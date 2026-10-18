# Worker Threads on In-Process Isolates

Status: active
Last reviewed: 2026-10-17
Owner: theMackabu

## Goal

`node:worker_threads` runs each `Worker` as a native thread that owns one `ant_t`
and one `uv_loop_t`, with Node-compatible messaging, lifecycle and termination.
The old process-spawning stub (JSON frames on stdout, no `Worker.postMessage`) is
gone.

## Isolate Ownership (prerequisites)

- Static GC roots are gone (`gc_register_root` removed). Every root is reached
  through isolate-owned visitors; registered-library namespaces are cached per
  isolate (`js->libraries`), and the library registry itself is shared and locked.
- Shapes: root shapes and the GC shape epoch are per isolate (`js->shapes`), so one
  isolate's sweep never prunes another's tree. The transition-entry pool and shape
  byte counter are thread-local because `ant_shape_release` has no isolate.
- The string intern table stays process-wide (interned pointers are compared
  across isolates and live forever). Lookups are lock-free over an open-addressed
  table; inserts take a mutex and growth publishes a new table, so readers on an
  old table see valid entries and fall back to the locked path on a miss.
- Module state that held per-isolate resources moved onto `ant_t` or behind a
  mutex: rpc servers/clients, sandboxes, lmdb handles and prototypes, N-API env
  (externals, wraps, adjust-external-memory), navigator locks, stream/net
  defaults, `process.env` (see below). `localStorage` and `sessionStorage` stay
  process-wide behind one mutex each. Racy one-time initialisers became
  `uv_once` or constant tables (signal map, WAMR engine, regex static accessors,
  `String.replace` dispatch, uuid hex table); uuidv7 state is locked.
- `ArrayBufferData.ref_count` is atomic so SharedArrayBuffers can be shared
  between threads. Console output buffers and several caches are thread-local;
  `ant_thread_cleanup()` frees them when a worker thread exits.
- The pointer cage is unchanged from master: one fixed 32 GiB reservation shared
  by every isolate (the same model as V8's shared pointer-compression cage), and
  `ant_cage_contains` stays the exact `offset < reserved size` check. A growable
  cage was tried and dropped: on macOS the address after the cage is often taken,
  and non-contiguous growth would need a slower or weaker containment check.
  `--cage-size=<GiB>` (default 32) sets the reservation before the first isolate
  is created. The only upper bound is the 47-bit NaN-box payload (131072 GiB); if
  the OS refuses a size, the reservation halves until it fits, as it does for the
  default. Reserving costs ~1 µs per GiB at startup.
- Isolates size their arenas instead (`ant_create_with_options`). The main
  isolate keeps a 4 GiB object arena (5.5 GiB of cage with the closure, upvalue
  and promise arenas, which scale at master's 1/8 ratio). Workers default to a
  512 MiB object arena (~3.9M live objects, ~704 MiB of cage), or
  `resourceLimits.maxOldGenerationSizeMb` clamped to 16 MiB–4 GiB; about 38
  default workers fit at once, 64 at 128 MiB. Only object, closure, upvalue and
  promise headers, strings and code live in the cage; array storage, overflow
  properties and ArrayBuffer bytes are outside it.
- An exhausted object or closure arena runs a full GC and retries once. A worker
  that still runs out reports `ERR_WORKER_OUT_OF_MEMORY` (built by the parent;
  the child does not serialize anything) and exits 1. If the cage has no room
  for a new isolate, the worker emits `ERR_WORKER_INIT_FAILED` and exits 1.

## Teardown

`js_destroy` closes every handle-owning module through its own close path before
objects are freed: net, `Ant.serve`, fetch/http client (`ant_http_request_abort`),
child processes (handles closed, children keep running, as in Node), fs watchers
and queued fs requests, readline, N-API (cleanup hooks LIFO, threadsafe functions),
message ports, broadcast channels, process signals/stdin tty, timers (twice: jobs
can be queued during the drain), and worker children (terminated and joined).
`Ant_Loop_Drain` waits for in-flight requests on owned loops before objects are
freed. Leaks found and fixed on the way: functions compiled without an active
code unit were never released (they pinned shape trees), closure upvalue arrays
and the remembered-coroutine set, coroutines awaiting promises that never settle,
`Ant.serve` connection state with an unresolved response, `net.Server` (no
finalizer), `fs.watchFile` watchers, zlib streams rooted forever, Map/Set iterator
state.

## Messaging

- `src/modules/wire.c`: byte serializer for cross-isolate structured clone.
  Back-references (cycles, shared refs), primitives incl. BigInt and -0, boxed
  primitives, Date, RegExp, Map, Set, Error (+cause, AggregateError.errors, own
  props; stack via the error's own accessor), arrays, plain objects (own
  enumerable string keys read with [[Get]]), ArrayBuffer (copy; transfer copies
  then detaches the source because buffers are registered per isolate),
  SharedArrayBuffer (shared `ArrayBufferData`), typed arrays, DataView, Blob/File,
  MessagePort (transfer only). Unsupported values throw `DataCloneError`.
- `src/modules/message_port.c`: a channel is two sides behind one mutex; each
  side's receiving isolate attaches a binding with its own `uv_async_t`, so a
  sender on any thread appends and signals under the lock. Transfer detaches the
  binding and the receiving isolate re-binds; queued messages wait in the channel.
  `MessagePort` is an `EventTarget` whose `.on/.once/.off` listeners receive
  `event.data` (entry flag `ENTRY_DATA_ARG` in `events.c`) in one registration
  order with `addEventListener`/`onmessage`; a `'message'` listener starts and
  refs the port. `BroadcastChannel` keeps a process-wide name registry; messages
  share one read-only wire.
- `MessageChannel`, `MessagePort` and `BroadcastChannel` are globals.

## Workers

- `src/modules/worker_threads.c`: a refcounted `ant_worker_t` shared by the
  parent (Worker object, parent `uv_async_t` for online/error/exit) and the child
  thread (control `uv_async_t` for termination). Messages use an entangled port
  pair; the parent side is an internal binding that emits on the Worker.
- The child sets `js->worker`, arms JIT interrupts before any compile, bootstraps,
  posts `online`, runs the entry (`file:` URL or path import, `data:` URL, or
  `eval` as CommonJS), runs the loop, runs process exit handlers, tears down and
  posts `exit`. The parent drains pending messages before emitting `exit`.
- `terminate()` sets the child's interrupt flag, wakes its loop and any
  `Atomics.wait` it is blocked in, and resolves with the exit code (1; 0 if the
  worker had not started; `undefined` if it had already exited).
- Worker mode hooks: `process.exit` and uncaught exceptions/unhandled rejections
  end the worker (the error is serialized to the parent's `'error'`; no listener
  makes it an uncaught exception in the parent); no signal handlers, stdin tty,
  `.env` loading or `Ant.serve` signal handlers in workers; `process.chdir`
  throws `ERR_WORKER_UNSUPPORTED_OPERATION`.
- Worker stdio: a thread-local output writer posts stdout/stderr bytes to the
  parent on the same event queue as `exit`, so output is never lost or reordered
  behind it. `worker.stdout`/`worker.stderr` are Readables; by default the parent
  writes the bytes to its own stdout/stderr and pushes them into the Readable only
  when it has `'data'` listeners, so unconsumed output never buffers. With
  `stdout: true`/`stderr: true` everything goes to the Readable.
- `process.env` in a worker is a private table seeded from `options.env` or the
  parent's env snapshot; `SHARE_ENV` uses the real environment, which is now
  accessed under one mutex. Child processes spawned from a worker inherit its env.

## Validation

- Node 22 parity: the comparison scripts used during development produced
  identical output to Node for MessagePort semantics, serialization, worker
  messaging, errors, exit codes, termination (busy loop, idle loop,
  `Atomics.wait`), env modes, SAB/Atomics across threads, nested workers,
  broadcast channels, transfers and 64 concurrent workers. Regression tests:
  `tests/test_message_port.mjs`, `tests/test_worker_threads.mjs`,
  `tests/test_structured_clone_props.mjs` (all also pass on Node 22),
  `examples/spec/worker_threads.js` (updated to Node semantics; Node fails the
  old synchronous-delivery assertions).
- Native: `isolate-libraries`, `isolate-threads` (4 threads × 3 isolates, JIT,
  GC, async, interning), `isolate-teardown` (15 scenarios destroyed with live
  handles).
- TSan (Apple clang; nix clang's TSan runtime crashes on this macOS) clean on the
  thread and worker tests; ASan clean on teardown and worker tests; `leaks` on a
  debug build shows only CLI-argument allocations and 208 bytes in libuv's
  FSEvents runner.

## Follow-ups

- `worker.stdin` is `null`; `stdin: true` is not implemented.
- `resourceLimits` enforces `stackSizeMb` and `maxOldGenerationSizeMb` (object
  arena size); `maxYoungGenerationSizeMb` and `codeRangeSizeMb` are reported
  only. Strings and code share the cage without a per-isolate cap.
- `getHeapSnapshot()` rejects; `postMessageToThread` and `worker.performance` are
  missing.
- `navigator.locks` is per isolate; Node shares Web Locks across threads.
- Workers are disabled in compiled executables (`js_esm_bundle_active`).
- zlib streams are still kept alive by a second, older reference; readline
  interfaces are only freed at isolate teardown, not on `close()`.
- `error-handoffs` C test fails on an `active_async_coro` assumption from the
  lazy async-coroutine work.
