# tlsuv Connect Timeout and Happy Eyeballs

Status: completed
Last reviewed: 2026-10-21
Owner: theMackabu

## Goal

Stop HTTPS requests to busy hosts from hanging until the caller aborts. About
1 in 12 to 16 cold `upm install` runs stalled for 30 s because one or two
connections sent a ClientHello that was never answered.

## Causes

- Ant never called `tlsuv_http_connect_timeout`, and tlsuv defaults it to 0
  (no timeout). An unanswered handshake stayed pending forever.
- tlsuv's connector (`src/connector.c`, `on_resolve`) opened a TCP connection
  to every resolved address at once (up to 16), kept the first that connected,
  and closed the rest without sending data. `registry.npmjs.org` resolves to
  24 Cloudflare addresses, so each install began with a burst of abandoned
  connections. The stalls hit only connections opened during that burst.

## Decisions

- `ANT_HTTP_CONNECT_TIMEOUT_MS` (10 s, matching Node's fetch) is set on every
  pooled HTTP client and on remote ESM imports. The tlsuv timer covers TCP
  connect plus the TLS handshake.
- `ant_http_should_retry` retries `UV_ETIMEDOUT` once for idempotent requests,
  even on fresh connections, so worst-case failure is about 20 s.
- `tlsuv-connector-happy-eyeballs.patch` replaces the all-at-once race with
  RFC 8305-style attempts: candidates interleave by address family, starting
  with the first answer's family. The next candidate starts on failure or
  after 250 ms. A per-request `uv_timer` drives this; the result callback runs
  from the timer's close callback so it fires exactly once, including on
  cancel.

## Validation

- A local server that accepts and never answers: the request fails after
  10 s + one retry (Ant 16.0 still pending after 25 s).
- One fetch to `registry.npmjs.org` opens 1 TCP connection (16.0: 3 to 7).
- Connector test under ASan on Linux: first candidate dropped, all dropped
  then canceled, cancel during attempt and during resolve, refused, unknown
  host. Each produced exactly one callback and no leaks.
- 40 cold large `upm install` runs: all succeeded, max 10.9 s.
- `tests/test_fetch_keepalive.mjs`, the focused TLS/HTTP/WebSocket/fetch
  tests, and `tests/harness/run.js` (only the known uninstalled-npm failures).
