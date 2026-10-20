# Worker Threads on In-Process Isolates

Status: active
Last reviewed: 2026-10-19
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
  isolate's sweep never prunes another's tree. The shape byte counter and the
  transition-entry pool live in a per-isolate `ant_shape_heap_t`; every shape
  points at its isolate's heap, so release and clone need no isolate argument and
  no thread-local access (thread-local storage cost 0.5–1.2% instructions on
  shape churn on macOS).
- `js_destroy` frees the shape tree only when the process keeps running (workers,
  embedders, native tests). The main entrypoints set `js->process_exiting` first,
  which skips it as master did: freeing every live shape at exit cost about 900
  instructions per shape (+18% on 200k uniquely-keyed live objects, +6% on the
  intern new-keys bench). `leaks --atExit` reports that tree on the main isolate.
- The string intern table is per isolate (`js->intern.table`): an open-addressed
  table with no atomics or locks, whose entries come from a bump arena of 64 KiB
  chunks and are freed with the isolate in `js_destroy`. Nothing compares interned
  pointers across isolates (values cross only through the wire serializer, which
  copies bytes), and a worker's keys no longer stay in a process-wide table after
  it exits. The cfunc registry keys derived records by name pointer, but only
  compares those pointers after the owning realm is released. On master the table
  was process-wide and its entries were never freed.
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
  default. Reserving costs ~1 µs per GiB at startup. The cage base is aligned to
  8 MiB so a cage offset shifted right by 23 indexes the arena chunk table.
- Arenas are lists of 8 MiB chunks (1 MiB on WASM) taken from the cage on
  demand (`include/arena.h`, `src/gc/arena.c`), so an isolate reserves only what
  it uses: an idle worker holds ~32 MiB of cage (four arenas, one chunk each).
  The per-isolate sizes (`ant_create_with_options`) are now byte limits on
  committed memory, not reservations: the main isolate keeps 4 GiB for objects
  with the closure, upvalue and promise arenas at master's 1/8 ratio; workers
  default to 512 MiB or `resourceLimits.maxOldGenerationSizeMb` clamped to
  16 MiB–4 GiB. About 970 idle workers fit in the 32 GiB cage (was 38) and 242
  in 8 GiB (was 3). Only object, closure, upvalue and promise headers, strings
  and code live in the cage; array storage, overflow properties and ArrayBuffer
  bytes are outside it.
- A process-wide table maps each 8 MiB cage slot to the owning arena's 32-bit id
  (bit 31 marks the arena's last, partially used chunk) and the chunk ordinal.
  Marking accepts a value with one load and compare for full chunks; pointers
  into the last chunk must also be below the bump cursor, because the sweep
  decommits the tail above the highest live object and stale references to dead
  objects there must not be read. Conservative scanning checks the ordinal,
  used extent and slot alignment. Entries are relaxed atomics: other isolates
  publish and clear their own slots while a scan runs.
- Arena chunks are only taken while the cage keeps 1/32 of its size free, so
  strings, code and pool blocks always have room. A full cage therefore shows up
  as an arena failure: a new worker emits `ERR_WORKER_INIT_FAILED`, a growing
  heap gets a full GC and then `ERR_WORKER_OUT_OF_MEMORY`. Without the reserve,
  pool allocations failed first and aborted the process.
- An exhausted object or closure arena runs a full GC and retries once. A worker
  that still runs out reports `ERR_WORKER_OUT_OF_MEMORY` (built by the parent;
  the child does not serialize anything) and exits 1. If the cage has no room
  for a new isolate, the worker emits `ERR_WORKER_INIT_FAILED` and exits 1.
- GC fixture cost of chunking: at most +0.75% instructions (survival-heavy
  patterns), cycles within ±1%; bench-v8 fixed work +0.1% instructions.

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

## Cross-Thread Services

- `postMessageToThread`: each isolate gets a mailbox (lock, queue, unref'd
  `uv_async_t`) in a process-wide registry keyed by thread id, created on the
  first `process.on('workerMessage')` listener or the first send. A request
  carries the wire value and a retained reply mailbox; the destination emits
  `workerMessage(value, source)`, swallows listener throws, and replies
  delivered / failed / errored. The sender keeps pending promises (GC-marked,
  async ref'd while any are pending) with an optional timeout timer. Closing an
  isolate answers its queued requests with `ERR_WORKER_MESSAGING_FAILED`.
- `stdin: true`: `worker.stdin` is a Writable whose `_write`/`_final` queue bytes
  or EOF on the worker under `w->lock` and wake `child_async`, which now drains
  stdin unless a terminate was requested. The worker's `process.stdin` is a plain
  Readable fed from that queue; without `stdin: true` it ends immediately. Reading
  it refs the worker loop until EOF, like Node.
- `navigator.locks`: one registry of resources (held list + FIFO queue) under a
  global mutex; each isolate is a client with an inbox and an unref'd async
  handle. Grants to the calling isolate run synchronously as before, grants to
  another isolate wake it. Adds `steal`, `signal`, `clientId` and the spec's
  NotSupportedError checks, and grants every compatible waiter. `query()` lists
  only the caller's locks (Node does the same). Unlike Node 26, an isolate's
  locks are released when it is destroyed; Node leaks them on worker exit.
- Compiled executables: the worker isolate activates the parent's read-only
  bundle, `worker_resolve_entry` maps absolute keys and traced relative paths to
  bundle keys (falling back to disk), and the tracer records
  `new Worker('./x')` edges.
- `resourceLimits` is reported like Node: the parent object echoes requested
  values with `-1` for unset ones; the worker sees effective values.

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
- Cross-thread services: `tests/test_worker_post_message_to_thread.cjs`,
  `tests/test_worker_stdin.cjs`, `tests/test_worker_performance.cjs` (pass on
  Node 22) and `tests/test_navigator_locks_threads.cjs` (passes on Node 26; the
  worker-exit release case runs on Ant only); the compiled-executable worker
  cases are in `tests/test_compile_basic.cjs`. TSan clean on all four.
- Native: `isolate-libraries`, `isolate-threads` (4 threads × 3 isolates, JIT,
  GC, async, interning), `isolate-teardown` (15 scenarios destroyed with live
  handles).
- TSan (Apple clang; nix clang's TSan runtime crashes on this macOS) clean on the
  thread and worker tests; ASan clean on teardown and worker tests; `leaks` on a
  debug build shows only CLI-argument allocations and 208 bytes in libuv's
  FSEvents runner.

## Follow-ups

- `maxYoungGenerationSizeMb` and `codeRangeSizeMb` are reported, not enforced:
  the nursery is an adaptive object-count threshold (~2 MiB, already below V8's
  young generation) and JIT code lives in growable per-isolate code arenas.
  Strings and code share the cage without a per-isolate cap.
- `getHeapSnapshot()` rejects; Ant has no heap snapshot writer.
- `readline` on a worker's `process.stdin` does not work: readline reads fd 0
  through `process_stdin_attach_reader`, which is disabled in workers.
- Main-thread `process.stdin` never emits `'end'` on piped input (pre-existing,
  also in the installed release).
- zlib streams are still kept alive by a second, older reference; readline
  interfaces are only freed at isolate teardown, not on `close()`.
- `error-handoffs` C test fails on an `active_async_coro` assumption from the
  lazy async-coroutine work.
