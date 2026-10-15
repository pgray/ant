#include <stdlib.h>
#include <libbase64.h>
#include "base64.h"

char *ant_base64_encode(const uint8_t *data, size_t len, size_t *out_len) {
  size_t encoded_len = 4 * ((len + 2) / 3);
  char *result = malloc(encoded_len + 1);
  if (!result) return NULL;
  
  base64_encode((const char *)data, len, result, out_len, 0);
  result[*out_len] = '\0';
  return result;
}

uint8_t *ant_base64_decode(const char *data, size_t len, size_t *out_len) {
  size_t decoded_len = (len / 4) * 3 + 3;
  uint8_t *result = malloc(decoded_len);
  if (!result) return NULL;
  
  if (!base64_decode(data, len, (char *)result, out_len, 0)) {
    free(result);
    return NULL;
  }
  
  return result;
}

char *ant_base64url_encode(const uint8_t *data, size_t len, size_t *out_len) {
  char *result = ant_base64_encode(data, len, out_len);
  if (!result) return NULL;

  for (size_t i = 0; i < *out_len; i++) {
    if (result[i] == '+') result[i] = '-';
    else if (result[i] == '/') result[i] = '_';
  }
  
  while (*out_len > 0 && result[*out_len - 1] == '=') (*out_len)--;
  result[*out_len] = '\0';
  
  return result;
}

static int base64_loose_value(unsigned char c) {
  if (c >= 'A' && c <= 'Z') return c - 'A';
  if (c >= 'a' && c <= 'z') return c - 'a' + 26;
  if (c >= '0' && c <= '9') return c - '0' + 52;
  if (c == '+' || c == '-') return 62;
  if (c == '/' || c == '_') return 63;
  return -1;
}

uint8_t *ant_base64_decode_loose(const char *data, size_t len, size_t *out_len) {
  uint8_t *result = malloc((len / 4) * 3 + 3);
  if (!result) return NULL;
  if (base64_decode(data, len, (char *)result, out_len, 0)) return result;

  size_t written = 0;
  uint32_t acc = 0;
  int bits = 0;

  for (size_t i = 0; i < len && data[i] != '='; i++) {
    int value = base64_loose_value((unsigned char)data[i]);
    if (value < 0) continue;
    
    acc = (acc << 6) | (uint32_t)value;
    bits += 6;
    
    if (bits >= 8) {
      bits -= 8;
      result[written++] = (uint8_t)(acc >> bits);
    }
  }

  *out_len = written;
  return result;
}

typedef enum {
  BASE64_END_FLUSH,
  BASE64_END_STOP,
  BASE64_END_ERROR,
} base64_end_t;

typedef struct {
  int values[4];
  int count;
  size_t out;
  size_t consumed;
} base64_decoder_t;

static inline bool base64_space(unsigned char c) {
  return c == ' ' || c == '\t' || c == '\n' || c == '\f' || c == '\r';
}

static bool base64_extra_bits(const int *values, int count) {
  return (count == 2 && (values[1] & 0x0f)) || (count == 3 && (values[2] & 0x03));
}

static size_t base64_skip_space(const char *src, size_t len, size_t i) {
  while (i < len && base64_space((unsigned char)src[i])) i++;
  return i;
}

static inline base64_end_t base64_fail(const char **error, const char *message) {
  *error = message;
  return BASE64_END_ERROR;
}

static int base64_value(unsigned char c, bool url) {
  if (c >= 'A' && c <= 'Z') return c - 'A';
  if (c >= 'a' && c <= 'z') return c - 'a' + 26;
  if (c >= '0' && c <= '9') return c - '0' + 52;
  if (c == (url ? '-' : '+')) return 62;
  if (c == (url ? '_' : '/')) return 63;
  return -1;
}

static size_t base64_flush(const int *values, int count, uint8_t *dst) {
  uint32_t acc = (uint32_t)values[0] << 18 | (uint32_t)values[1] << 12;
  if (count > 2) acc |= (uint32_t)values[2] << 6;
  if (count > 3) acc |= (uint32_t)values[3];
  dst[0] = (uint8_t)(acc >> 16);
  if (count > 2) dst[1] = (uint8_t)(acc >> 8);
  if (count > 3) dst[2] = (uint8_t)acc;
  return (size_t)count - 1;
}

static base64_end_t base64_end_of_input(
  const base64_decoder_t *d, ant_base64_last_chunk_t last_chunk, const char **error
) {
  if (d->count == 0) return BASE64_END_FLUSH;
  if (last_chunk == ANT_BASE64_STOP_BEFORE_PARTIAL) return BASE64_END_STOP;
  if (d->count == 1) return base64_fail(error, "The base64 input terminates with a single character");
  if (last_chunk == ANT_BASE64_STRICT) return base64_fail(error, "The base64 input is missing padding");
  return BASE64_END_FLUSH;
}

static base64_end_t base64_end_of_padding(
  const base64_decoder_t *d, const char *src, size_t len, size_t i,
  ant_base64_last_chunk_t last_chunk, const char **error
) {
  if (d->count < 2) return base64_fail(error, "Unexpected padding in base64 input");
  i = base64_skip_space(src, len, i);

  if (d->count == 2) {
    if (i == len) {
      if (last_chunk == ANT_BASE64_STOP_BEFORE_PARTIAL) return BASE64_END_STOP;
      return base64_fail(error, "The base64 input is missing padding");
    }
    if (src[i] == '=') i = base64_skip_space(src, len, i + 1);
  }

  if (i < len) return base64_fail(error, "Unexpected data after base64 padding");
  if (last_chunk == ANT_BASE64_STRICT && base64_extra_bits(d->values, d->count))
    return base64_fail(error, "The base64 input terminates with non-zero padding bits");
  return BASE64_END_FLUSH;
}

static inline base64_end_t base64_decode_chunks(
  base64_decoder_t *d, const char *src, size_t len, bool url,
  ant_base64_last_chunk_t last_chunk, uint8_t *dst, size_t max_len, const char **error
) {
  size_t i = 0;

  for (;;) {
    i = base64_skip_space(src, len, i);
    if (i == len) return base64_end_of_input(d, last_chunk, error);

    unsigned char c = (unsigned char)src[i++];
    if (c == '=') return base64_end_of_padding(d, src, len, i, last_chunk, error);

    int value = base64_value(c, url);
    if (value < 0) return base64_fail(error, "Found a character that cannot be part of a valid base64 string");
    if (max_len - d->out < (size_t)d->count) return BASE64_END_STOP;

    d->values[d->count++] = value;
    if (d->count < 4) continue;

    d->out += base64_flush(d->values, 4, dst + d->out);
    d->count = 0;
    d->consumed = i;
    if (d->out == max_len) return BASE64_END_STOP;
  }
}

bool ant_base64_decode_into(
  const char *src, size_t len, bool url, ant_base64_last_chunk_t last_chunk,
  uint8_t *dst, size_t max_len, size_t *read, size_t *written, const char **error
) {
  base64_decoder_t d = { .count = 0 };
  *error = NULL;

  base64_end_t end = max_len == 0
    ? BASE64_END_STOP
    : base64_decode_chunks(&d, src, len, url, last_chunk, dst, max_len, error);

  if (end == BASE64_END_ERROR) return false;
  
  if (end == BASE64_END_FLUSH) {
    if (d.count > 0) d.out += base64_flush(d.values, d.count, dst + d.out);
    d.consumed = len;
  }

  *read = d.consumed;
  *written = d.out;
  
  return true;
}
