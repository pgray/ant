# Stream Close Semantics and Lazy Zlib Init

Status: completed
Last reviewed: 2026-10-20
Owner: theMackabu

## Outcome

- Readables no longer emit `'close'` straight after `'end'`. Like Node, the end
  of the readable side calls `destroy()` when `autoDestroy` is on and the
  writable side is absent or finished; otherwise the writable `'finish'` does.
  Duplexes (PassThrough, Transform, zlib) emitted `'close'` twice before; now
  once, and `stream.destroyed` is true after a normal end.
- `allowHalfOpen: false` ends the writable side on the next tick after the
  readable side ends (previously ignored).
- `isDisturbed()` follows Node: a stream that ended without reading data is not
  disturbed even though autoDestroy destroyed it.
- Writes after `end()` / `destroy()` fail with `ERR_STREAM_WRITE_AFTER_END` /
  `ERR_STREAM_DESTROYED` (Node's messages) and the callback runs on a later
  microtask, not synchronously.
- Zlib (non-brotli) streams create their native state on first use
  (`zlib_stream_ready`): an idle `createGzip()` costs ~12.6 KB RSS instead of
  ~222 KB, because zlib-ng clears a fixed 128 KB hash table and the 64 KB
  `prev` array in `deflateInit2`. Node: ~21 KB.
- `_handle.close()` frees the native state without destroying the stream (Node
  parity) and later writes, flushes, `params()` and `_processChunk()` fail with
  `ERR_ZLIB_BINDING_CLOSED` (Node's former code for this case; current Node
  aborts on an internal assertion). After `close()`/`destroy()`, `_handle` is
  `null` and `flush(cb)` still calls `cb`.
- Dictionaries are preset only for deflate, deflateRaw and inflateRaw, as in
  Node's binding; `createGzip({ dictionary })` and `gzipSync(..., { dictionary })`
  no longer throw. Streaming inflate with a dictionary decoded nothing before
  (the `Z_NEED_DICT` retry `continue`d out of the output loop); both stream
  paths now retry `inflate` right after `inflateSetDictionary`.
- Stream inflate/deflate errors carry Node's `code`, `errno` and zlib message
  (`Z_NEED_DICT` "Missing dictionary", `Z_DATA_ERROR` "incorrect header check").

## Validation

- `tests/test_stream_lifecycle_parity.cjs` and
  `tests/test_zlib_stream_lifecycle.cjs` pass on Node 22 and Ant; the
  binding-closed cases are Ant-only because Node aborts there.
- Every `tests/test_*` file run against the build and the previous binary: no
  regressions (933 pass; the remaining failures fail identically before).
- Spec suite 4343/4343, harness unchanged apart from environment failures,
  ASan clean on all zlib/stream tests.

## Remaining

- For a Duplex whose two sides finish in the same turn, Ant emits `'end'` before
  `'finish'`; Node emits `'finish'` first.
