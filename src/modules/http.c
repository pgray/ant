#include <uv.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <uthash.h>
#include <tlsuv/http.h>
#include <tlsuv/tlsuv.h>

#include "modules/http.h"
#include "streams/brotli.h"

static constexpr int HTTP_POOL_MAX_CONNS_PER_HOST = 256;
static constexpr uint64_t HTTP_POOL_IDLE_MS = 4000;

typedef struct ant_http_host ant_http_host_t;
typedef struct ant_http_conn ant_http_conn_t;

struct ant_http_pool {
  uv_loop_t *loop;
  ant_http_host_t *hosts;
  bool closing;
};

struct ant_http_host {
  char *key;
  ant_http_pool_t *pool;
  ant_http_conn_t *conns;
  int conn_count;
  ant_http_request_t *pending_head;
  ant_http_request_t *pending_tail;
  UT_hash_handle hh;
};

struct ant_http_conn {
  tlsuv_http_t client;
  uv_timer_t idle_timer;
  ant_http_host_t *host;
  ant_http_request_t *req;
  ant_http_conn_t *next;
  int open_handles;
  bool closing;
};

struct ant_http_request_s {
  uv_loop_t *loop;
  ant_http_pool_t *pool;
  ant_http_conn_t *conn;
  ant_http_host_t *host;
  tlsuv_http_req_t *req;
  ant_http_request_t *next_pending;
  ant_http_response_t response;

  ant_http_response_cb on_response;
  ant_http_body_cb on_body;
  ant_http_complete_cb on_complete;

  void *user_data;
  char *error_message;
  brotli_stream_state_t *brotli_decoder;

  char *host_url;
  char *method;
  char *path;
  ant_http_header_t *headers;
  uint8_t *body;
  size_t body_len;
  uv_timer_t done_timer;

  ant_http_result_t result;
  int error_code;

  bool completed;
  bool canceled;
  bool decode_brotli;
  bool chunked;
  bool pending;
  bool reused;
  bool retried;
  bool got_response;
};

static void ant_http_resp_cb(tlsuv_http_resp_t *resp, void *data);
static void ant_http_finish(ant_http_request_t *req, int error_code, const char *error_message);

void ant_http_headers_free(ant_http_header_t *headers) {
while (headers) {
  ant_http_header_t *next = headers->next;
  if (headers->name != headers->storage) {
    free(headers->name);
    free(headers->value);
  }
  free(headers);
  headers = next;
}}

static void ant_http_request_free(ant_http_request_t *req) {
  if (!req) return;
  free((char *)req->response.status_text);
  ant_http_headers_free((ant_http_header_t *)req->response.headers);
  ant_http_headers_free(req->headers);
  free(req->error_message);
  free(req->host_url);
  free(req->method);
  free(req->path);
  free(req->body);
  if (req->brotli_decoder) brotli_stream_state_destroy(req->brotli_decoder);
  free(req);
}

static void ant_http_upload_chunk_cb(tlsuv_http_req_t *http_req, char *body, ssize_t len) {
  free(body);
}

static char *ant_http_build_host_url(const struct tlsuv_url_s *url) {
  size_t size = 0;
  char port_buf[16] = {0};
  int port_len = 0;

  if (url->port != 0) port_len = snprintf(port_buf, sizeof(port_buf), ":%u", url->port);
  size = url->scheme_len + 3 + url->hostname_len + (size_t)port_len + 1;

  char *host_url = malloc(size);
  if (!host_url) return NULL;
  snprintf(
    host_url, size, "%.*s://%.*s%s",
    (int)url->scheme_len, url->scheme,
    (int)url->hostname_len, url->hostname,
    port_buf
  );

  return host_url;
}

static char *ant_http_build_path(const struct tlsuv_url_s *url) {
  const char *path = (url->path && url->path_len > 0) ? url->path : "/";
  size_t path_len = (url->path && url->path_len > 0) ? url->path_len : 1;
  size_t query_extra = url->query && url->query_len > 0 ? (size_t)url->query_len + 1 : 0;
  char *request_path = malloc(path_len + query_extra + 1);
  if (!request_path) return NULL;

  memcpy(request_path, path, path_len);
  if (query_extra > 0) {
    request_path[path_len] = '?';
    memcpy(request_path + path_len + 1, url->query, url->query_len);
    request_path[path_len + query_extra] = '\0';
  } else request_path[path_len] = '\0';

  return request_path;
}

static ant_http_header_t *ant_http_header_dup(const char *name, const char *value) {
  ant_http_header_t *hdr = calloc(1, sizeof(*hdr));
  if (!hdr) return NULL;

  hdr->name = strdup(name ? name : "");
  hdr->value = strdup(value ? value : "");

  if (!hdr->name || !hdr->value) {
    free(hdr->name);
    free(hdr->value);
    free(hdr);
    return NULL;
  }

  return hdr;
}

static ant_http_header_t *ant_http_copy_headers(tlsuv_http_resp_t *resp) {
  ant_http_header_t *head = NULL;
  ant_http_header_t **tail = &head;
  tlsuv_http_hdr *hdr = NULL;

  LIST_FOREACH(hdr, &resp->headers, _next) {
    ant_http_header_t *copy = ant_http_header_dup(hdr->name, hdr->value);
    if (!copy) {
      ant_http_headers_free(head);
      return NULL;
    }

    *tail = copy;
    tail = &copy->next;
  }

  return head;
}

static bool ant_http_copy_request_headers(ant_http_request_t *req, const ant_http_header_t *headers) {
  ant_http_header_t **tail = &req->headers;
  for (const ant_http_header_t *hdr = headers; hdr; hdr = hdr->next) {
    if (hdr->name && strcasecmp(hdr->name, "host") == 0) continue;
    ant_http_header_t *copy = ant_http_header_dup(hdr->name, hdr->value);
    if (!copy) return false;
    *tail = copy;
    tail = &copy->next;
  }
  return true;
}

static const char *ant_http_find_header(const ant_http_header_t *headers, const char *name) {
  for (const ant_http_header_t *entry = headers; entry; entry = entry->next) {
    if (entry->name && name && strcasecmp(entry->name, name) == 0) return entry->value;
  }
  return NULL;
}

static bool ant_http_method_idempotent(const char *method) {
  static const char *const methods[] = { "GET", "HEAD", "OPTIONS", "PUT", "DELETE", "TRACE" };
  for (size_t i = 0; i < sizeof(methods) / sizeof(methods[0]); i++)
    if (strcasecmp(method, methods[i]) == 0) return true;
  return false;
}

static uv_handle_t *ant_http_conn_io(ant_http_conn_t *conn) {
  uv_handle_t *tr = conn->client.tr;
  if (!tr) return NULL;
  if (!conn->client.ssl) return tr;
  uv_handle_t *watcher = (uv_handle_t *)&((tlsuv_stream_t *)tr)->watcher;
  return uv_handle_get_type(watcher) == UV_UNKNOWN_HANDLE ? NULL : watcher;
}

static void ant_http_host_maybe_free(ant_http_host_t *host) {
  if (!host || host->pool->closing || host->conns || host->pending_head) return;
  HASH_DEL(host->pool->hosts, host);
  free(host->key);
  free(host);
}

static ant_http_conn_t *ant_http_conn_new(uv_loop_t *loop, ant_http_host_t *host, const char *host_url);
static void ant_http_start_or_fail(ant_http_request_t *req, ant_http_conn_t *conn);

static ant_http_request_t *ant_http_host_dequeue(ant_http_host_t *host) {
  ant_http_request_t *next = host->pending_head;
  if (!next) return NULL;
  host->pending_head = next->next_pending;
  if (!host->pending_head) host->pending_tail = NULL;
  next->next_pending = NULL;
  next->pending = false;
  return next;
}

static void ant_http_host_kick(ant_http_host_t *host) {
  while (!host->pool->closing && host->pending_head && host->conn_count < HTTP_POOL_MAX_CONNS_PER_HOST) {
    ant_http_conn_t *conn = ant_http_conn_new(host->pool->loop, host, host->pending_head->host_url);
    ant_http_request_t *next = ant_http_host_dequeue(host);
    if (!conn) {
      ant_http_finish(next, UV_ENOMEM, "out of memory");
      continue;
    }
    ant_http_start_or_fail(next, conn);
  }
}

static void ant_http_conn_handle_closed(ant_http_conn_t *conn) {
  if (--conn->open_handles == 0) free(conn);
}

static void ant_http_conn_timer_closed(uv_handle_t *handle) {
  ant_http_conn_handle_closed(handle->data);
}

static void ant_http_conn_client_closed(tlsuv_http_t *client) {
  ant_http_conn_handle_closed(client->data);
}

static ant_http_host_t *ant_http_conn_shutdown(ant_http_conn_t *conn) {
  if (conn->closing) return NULL;
  conn->closing = true;

  ant_http_host_t *host = conn->host;
  if (host) {
    for (ant_http_conn_t **it = &host->conns; *it; it = &(*it)->next) {
      if (*it != conn) continue;
      *it = conn->next;
      break;
    }
    host->conn_count--;
    conn->host = NULL;
  }

  if (conn->req) {
    ant_http_request_t *req = conn->req;
    conn->req = NULL;
    req->conn = NULL;
    if (req->req) req->req->data = NULL;
    req->req = NULL;
  }

  uv_close((uv_handle_t *)&conn->idle_timer, ant_http_conn_timer_closed);
  if (tlsuv_http_close(&conn->client, ant_http_conn_client_closed) != 0) ant_http_conn_handle_closed(conn);
  return host;
}

static void ant_http_conn_close(ant_http_conn_t *conn) {
  ant_http_host_t *host = ant_http_conn_shutdown(conn);
  if (host) {
    ant_http_host_kick(host);
    ant_http_host_maybe_free(host);
  }
}

static void ant_http_conn_idle_cb(uv_timer_t *timer) {
  ant_http_conn_t *conn = timer->data;
  if (!conn->req) ant_http_conn_close(conn);
}

static ant_http_conn_t *ant_http_conn_new(uv_loop_t *loop, ant_http_host_t *host, const char *host_url) {
  ant_http_conn_t *conn = calloc(1, sizeof(*conn));
  if (!conn) return NULL;

  if (tlsuv_http_init(loop, &conn->client, host_url) != 0) {
    free(conn);
    return NULL;
  }

  conn->client.data = conn;
  tlsuv_http_connect_timeout(&conn->client, ANT_HTTP_CONNECT_TIMEOUT_MS);
  tlsuv_http_header(&conn->client, "Accept-Encoding", NULL);
  tlsuv_http_header(&conn->client, "User-Agent", NULL);
  
  uv_timer_init(loop, &conn->idle_timer);
  uv_unref((uv_handle_t *)&conn->idle_timer);
  
  conn->idle_timer.data = conn;
  conn->open_handles = 2;

  if (host) {
    tlsuv_http_idle_keepalive(&conn->client, -1);
    conn->host = host;
    conn->next = host->conns;
    host->conns = conn;
    host->conn_count++;
  }
  return conn;
}

static int ant_http_dispatch(ant_http_request_t *req, ant_http_conn_t *conn) {
  conn->req = req;
  req->conn = conn;
  req->reused = conn->client.tr != NULL;
  uv_timer_stop(&conn->idle_timer);

  uv_handle_t *io = ant_http_conn_io(conn);
  if (io) uv_ref(io);

  req->req = tlsuv_http_req(&conn->client, req->method, req->path, ant_http_resp_cb, req);
  if (!req->req) return UV_ENOMEM;

  req->req->data = req;
  if (req->chunked) tlsuv_http_req_header(req->req, "transfer-encoding", "chunked");
  for (const ant_http_header_t *hdr = req->headers; hdr; hdr = hdr->next)
    tlsuv_http_req_header(req->req, hdr->name, hdr->value);

  if (req->body && req->body_len > 0)
    return tlsuv_http_req_data(req->req, (const char *)req->body, req->body_len, NULL);
  return 0;
}

static bool ant_http_conn_has_pending_read(ant_http_conn_t *conn) {
  uv_handle_t *tr = conn->client.tr;
  uv_os_sock_t sock;
  if (!tr) return false;

  if (conn->client.ssl) sock = ((tlsuv_stream_t *)tr)->sock;
  else {
    uv_os_fd_t fd;
    if (uv_fileno(tr, &fd) != 0) return false;
    sock = (uv_os_sock_t)(intptr_t)fd;
  }

#ifdef _WIN32
  u_long available = 0;
  return ioctlsocket(sock, FIONREAD, &available) == 0 && available > 0;
#else
  char byte;
  ssize_t r;
  do r = recv(sock, &byte, 1, MSG_PEEK | MSG_DONTWAIT);
  while (r < 0 && errno == EINTR);
  if (r >= 0) return true;
  return errno != EAGAIN && errno != EWOULDBLOCK && errno != ENOTCONN && errno != ENOTSOCK;
#endif
}

static ant_http_conn_t *ant_http_host_idle_conn(ant_http_host_t *host) {
  ant_http_conn_t *fallback = NULL;
  for (ant_http_conn_t *conn = host->conns; conn;) {
    ant_http_conn_t *next = conn->next;
    if (conn->req || conn->closing) {
      conn = next;
      continue;
    }
    
    if (conn->client.tr && ant_http_conn_has_pending_read(conn)) {
      ant_http_conn_shutdown(conn);
      conn = host->conns;
      fallback = NULL;
      continue;
    }
    
    if (conn->client.tr) return conn;
    if (!fallback) fallback = conn;
    conn = next;
  }
  
  return fallback;
}

static void ant_http_start_or_fail(ant_http_request_t *req, ant_http_conn_t *conn) {
  int rc = ant_http_dispatch(req, conn);
  if (rc != 0) ant_http_finish(req, rc, uv_strerror(rc));
}

static void ant_http_conn_release(ant_http_conn_t *conn) {
  ant_http_host_t *host = conn->host;
  if (host && host->pending_head && ant_http_conn_has_pending_read(conn)) {
    ant_http_conn_close(conn);
    return;
  }

  ant_http_request_t *next = host ? ant_http_host_dequeue(host) : NULL;

  if (next) {
    ant_http_start_or_fail(next, conn);
    return;
  }

  uv_handle_t *io = ant_http_conn_io(conn);
  if (io) uv_unref(io);
  uv_timer_start(&conn->idle_timer, ant_http_conn_idle_cb, HTTP_POOL_IDLE_MS, 0);
}

static void ant_http_pending_remove(ant_http_request_t *req) {
  ant_http_host_t *host = req->host;
  if (!req->pending || !host) return;
  ant_http_request_t *prev = NULL;
  for (ant_http_request_t *it = host->pending_head; it; prev = it, it = it->next_pending) {
    if (it != req) continue;
    if (prev) prev->next_pending = it->next_pending;
    else host->pending_head = it->next_pending;
    if (host->pending_tail == it) host->pending_tail = prev;
    break;
  }
  req->pending = false;
  req->next_pending = NULL;
  ant_http_host_maybe_free(host);
}

static void ant_http_done_closed(uv_handle_t *handle) {
  ant_http_request_t *req = handle->data;
  if (req->on_complete) req->on_complete(
    req, req->result, req->error_code,
    req->error_message,
    req->user_data
  );
  ant_http_request_free(req);
}

static void ant_http_done_cb(uv_timer_t *timer) {
  uv_close((uv_handle_t *)timer, ant_http_done_closed);
}

static void ant_http_finish(ant_http_request_t *req, int error_code, const char *error_message) {
  if (!req || req->completed) return;
  req->completed = true;
  req->error_code = error_code;

  if (error_code == 0) req->result = ANT_HTTP_RESULT_OK;
  else if (req->canceled) req->result = ANT_HTTP_RESULT_ABORTED;
  else req->result = ANT_HTTP_RESULT_NETWORK_ERROR;

  free(req->error_message);
  req->error_message = error_message ? strdup(error_message) : NULL;

  if (req->pending) ant_http_pending_remove(req);

  ant_http_conn_t *conn = req->conn;
  if (conn) {
    conn->req = NULL;
    req->conn = NULL;
    if (req->req) req->req->data = NULL;
    req->req = NULL;
    if (error_code != 0 || !conn->host) ant_http_conn_close(conn);
    else ant_http_conn_release(conn);
  }

  uv_timer_init(req->loop, &req->done_timer);
  req->done_timer.data = req;
  uv_timer_start(&req->done_timer, ant_http_done_cb, 0, 0);
}

static bool ant_http_should_retry(const ant_http_request_t *req, int code) {
  if (req->retried || req->chunked || req->got_response || req->canceled) return false;
  if (!req->conn || !req->conn->host || req->conn->host->pool->closing) return false;
  if (!ant_http_method_idempotent(req->method)) return false;

  return req->reused && (
    code == UV_EOF || code == UV_ECONNRESET || code == UV_EPIPE ||
    code == UV_ECONNABORTED || code == UV_ETIMEDOUT
  );
}

static void ant_http_retry(ant_http_request_t *req) {
  ant_http_conn_t *old = req->conn;
  ant_http_conn_t *conn = ant_http_conn_new(req->loop, old->host, req->host_url);
  req->retried = true;
  ant_http_conn_close(old);

  if (!conn) {
    ant_http_finish(req, UV_ENOMEM, "out of memory");
    return;
  }
  ant_http_start_or_fail(req, conn);
}

static int ant_http_brotli_body_cb(void *ctx, const uint8_t *chunk, size_t len) {
  ant_http_request_t *req = (ant_http_request_t *)ctx;
  if (req->on_body && len > 0) req->on_body(req, chunk, len, req->user_data);
  return 0;
}

static void ant_http_tlsuv_body_cb(tlsuv_http_req_t *http_req, char *body, ssize_t len) {
  ant_http_request_t *req = http_req ? (ant_http_request_t *)http_req->data : NULL;
  if (!req) return;

  if (len == UV_EOF) {
    if (req->decode_brotli && req->brotli_decoder &&
      brotli_stream_finish(req->brotli_decoder, ant_http_brotli_body_cb, req) != 0) {
      ant_http_finish(req, UV_EINVAL, "brotli decompression failed");
      return;
    }

    ant_http_finish(req, 0, NULL);
    return;
  }

  if (len < 0) {
    ant_http_finish(req, (int)len, uv_strerror((int)len));
    return;
  }

  if (req->decode_brotli && req->brotli_decoder) {
    if (brotli_stream_process(
      req->brotli_decoder, (const uint8_t *)body, (size_t)len,
      ant_http_brotli_body_cb, req) != 0
    ) ant_http_finish(req, UV_EINVAL, "brotli decompression failed");
    return;
  }

  if (req->on_body && len > 0) req->on_body(req, (const uint8_t *)body, (size_t)len, req->user_data);
}

static void ant_http_resp_cb(tlsuv_http_resp_t *resp, void *data) {
  ant_http_request_t *req = (ant_http_request_t *)resp->req->data;

  const char *content_encoding = NULL;
  if (!req) return;

  if (resp->code < 0) {
    if (ant_http_should_retry(req, resp->code)) ant_http_retry(req);
    else ant_http_finish(req, resp->code, uv_strerror(resp->code));
    return;
  }

  req->got_response = true;
  req->response.status = resp->code;
  req->response.status_text = resp->status ? strdup(resp->status) : strdup("");
  req->response.headers = ant_http_copy_headers(resp);

  if (!req->response.status_text || (resp->headers.lh_first && !req->response.headers)) {
    ant_http_finish(req, UV_ENOMEM, "out of memory");
    return;
  }

  content_encoding = ant_http_find_header(req->response.headers, "content-encoding");
  if (content_encoding && strcasecmp(content_encoding, "br") == 0) {
    req->brotli_decoder = brotli_stream_state_new(true);
    if (!req->brotli_decoder) {
      ant_http_finish(req, UV_ENOMEM, "out of memory");
      return;
    }
    req->decode_brotli = 1;
  }

  resp->body_cb = ant_http_tlsuv_body_cb;
  if (req->on_response) req->on_response(req, &req->response, req->user_data);
}

uv_loop_t *Ant_Http_RequestLoop(const ant_http_request_t *req) {
  return req ? req->loop : NULL;
}

int ant_http_request_cancel(ant_http_request_t *req) {
  if (!req || req->completed) return 0;
  req->canceled = true;
  if (!req->req || !req->conn) {
    ant_http_finish(req, UV_ECANCELED, uv_strerror(UV_ECANCELED));
    return 0;
  }
  return tlsuv_http_req_cancel(&req->conn->client, req->req);
}

void ant_http_request_abort(ant_http_request_t *req) {
  if (!req || req->completed) return;
  req->canceled = true;
  ant_http_finish(req, UV_ECANCELED, "request aborted");
}

int ant_http_request_write(ant_http_request_t *req, const uint8_t *chunk, size_t len) {
  uint8_t *copy = NULL;
  int rc = 0;

  if (!req || !req->req || req->completed) return UV_EINVAL;
  if (len == 0) return 0;

  copy = malloc(len);
  if (!copy) return UV_ENOMEM;
  memcpy(copy, chunk, len);

  rc = tlsuv_http_req_data(req->req, (const char *)copy, len, ant_http_upload_chunk_cb);
  if (rc != 0) free(copy);
  return rc;
}

void ant_http_request_end(ant_http_request_t *req) {
  if (!req || !req->req || req->completed) return;
  tlsuv_http_req_end(req->req);
}

ant_http_pool_t *ant_http_pool_create(uv_loop_t *loop) {
  ant_http_pool_t *pool = calloc(1, sizeof(*pool));
  if (pool) pool->loop = loop;
  return pool;
}

void ant_http_pool_free(ant_http_pool_t *pool) {
  if (!pool) return;
  pool->closing = true;

  ant_http_host_t *host, *tmp;
  HASH_ITER(hh, pool->hosts, host, tmp) {
    while (host->pending_head) {
      ant_http_request_t *req = host->pending_head;
      req->canceled = true;
      ant_http_finish(req, UV_ECANCELED, "request aborted");
    }
    while (host->conns) {
      ant_http_conn_t *conn = host->conns;
      if (conn->req) {
        conn->req->canceled = true;
        ant_http_finish(conn->req, UV_ECANCELED, "request aborted");
      } else ant_http_conn_close(conn);
    }
    HASH_DEL(pool->hosts, host);
    free(host->key);
    free(host);
  }
  free(pool);
}

static ant_http_host_t *ant_http_pool_host(ant_http_pool_t *pool, const char *key) {
  ant_http_host_t *host = NULL;
  HASH_FIND_STR(pool->hosts, key, host);
  if (host) return host;

  host = calloc(1, sizeof(*host));
  if (!host) return NULL;
  host->key = strdup(key);
  if (!host->key) {
    free(host);
    return NULL;
  }
  host->pool = pool;
  HASH_ADD_KEYPTR(hh, pool->hosts, host->key, strlen(host->key), host);
  return host;
}

int ant_http_request_start(
  uv_loop_t *loop,
  const ant_http_request_options_t *options,
  ant_http_response_cb on_response,
  ant_http_body_cb on_body,
  ant_http_complete_cb on_complete,
  void *user_data,
  ant_http_request_t **out_req
) {
  struct tlsuv_url_s parsed = {0};
  ant_http_request_t *req = NULL;

  if (out_req) *out_req = NULL;
  if (!loop || !options || !options->method || !options->url) return UV_EINVAL;
  if (tlsuv_parse_url(&parsed, options->url) != 0) return UV_EINVAL;
  if (!parsed.scheme || !parsed.hostname) return UV_EINVAL;

  req = calloc(1, sizeof(ant_http_request_t));
  if (!req) return UV_ENOMEM;

  req->on_response = on_response;
  req->on_body = on_body;
  req->on_complete = on_complete;
  req->user_data = user_data;
  req->loop = loop;
  req->chunked = options->chunked_body;
  req->pool = options->pool && !options->pool->closing ? options->pool : NULL;

  req->host_url = ant_http_build_host_url(&parsed);
  req->path = ant_http_build_path(&parsed);
  req->method = strdup(options->method);
  bool ok = req->host_url && req->path && req->method && ant_http_copy_request_headers(req, options->headers);

  if (ok && options->body && options->body_len > 0) {
    req->body = malloc(options->body_len);
    if (req->body) {
      memcpy(req->body, options->body, options->body_len);
      req->body_len = options->body_len;
    } else ok = false;
  }

  if (!ok) {
    ant_http_request_free(req);
    return UV_ENOMEM;
  }

  ant_http_host_t *host = req->pool ? ant_http_pool_host(req->pool, req->host_url) : NULL;
  ant_http_conn_t *conn = host ? ant_http_host_idle_conn(host) : NULL;

  if (!conn && host && host->conn_count >= HTTP_POOL_MAX_CONNS_PER_HOST && !req->chunked) {
    req->host = host;
    req->pending = true;
    if (host->pending_tail) host->pending_tail->next_pending = req;
    else host->pending_head = req;
    host->pending_tail = req;
    if (out_req) *out_req = req;
    return 0;
  }

  if (!conn) conn = ant_http_conn_new(loop, host, req->host_url);
  if (!conn) {
    ant_http_host_maybe_free(host);
    ant_http_request_free(req);
    return UV_ENOMEM;
  }

  req->host = host;
  ant_http_start_or_fail(req, conn);
  if (out_req) *out_req = req;
  return 0;
}
