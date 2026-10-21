# Async Context: AsyncLocalStorage and async_hooks

Status: active (planned)
Last reviewed: 2026-10-20
Owner: theMackabu

## Goal

Implement `node:async_hooks` with Node 24 semantics: `AsyncLocalStorage`
propagating through every async continuation, a working `AsyncResource`, async
ids, and an opt-in `createHook`. Hard constraints:

- No growth of the promise state (`ant_promise_state_t` stays 80 bytes; its
  `static_assert` in `include/object.h` stays).
- Programs that never construct an `AsyncLocalStorage` or enable a hook pay
  only a few instructions per continuation (target < 1% on the microbenchmarks
  below, measured as instructions retired).
- No JIT changes.

## Current State

`src/modules/async_hooks.c` is a stub "just enough for react":

- `AsyncLocalStorage.run` stores the value in the instance's `SLOT_DATA` and
  restores it when the callback returns, so stores are lost at the first
  `await`, `.then`, `queueMicrotask`, `nextTick`, timer or I/O callback; only
  synchronous nesting works.
- `AsyncResource.bind` throws ("Class constructor cannot be invoked without
  'new'"); `runInAsyncScope` just calls the function.
- `createHook` returns an object whose `enable`/`disable` do nothing;
  `executionAsyncId()` is 1, `triggerAsyncId()` 0, ids on resources 0.
- `napi_make_callback` ignores `async_context` (`src/napi/async.c:372`);
  `napi_async_init` only allocates a struct.

Probe (`als.run('A', async () => ...)`), Node 22 vs Ant:

```
node: sync=A after-await=A after-timer-await=A then=A microtask=A nextTick=A
      fs-callback=A setImmediate=A setTimeout=A nested-timer=B bound=A
      outside=- emitter-listener=-
ant:  sync=A, everything else '-'
```

Baselines on this machine (instructions per op, `async_paths.js`, release
build, 2026-10-20): `await` ~1,070, `.then` ~3,900 (includes the derived
promise and closure), `queueMicrotask` ~550, `setImmediate` ~2,800.

## Design

### Context frames

One value per isolate, `js->async_frame` (mutable root; GC-marked):

- A frame is an immutable flat array `[storage0, store0, storage1, store1,
  ...]`, or `undefined` for the empty frame. Frames are small (one to three
  storages in practice), so `getStore()` is a linear scan by identity.
- `run(store, fn, ...args)`: copy the current frame with the entry for this
  storage replaced or appended, install it, call `fn`, restore the previous
  frame (also on throw). `exit(fn)` is `run` with the entry removed.
- `enterWith(store)` replaces `js->async_frame` for the rest of the current
  synchronous execution and its continuations (Node semantics).
- `disable()` removes this storage from the current frame and marks the
  instance disabled so `getStore()` returns `undefined` until the next `run`.
- `js->async_frame_used` (bool) flips on the first `run`/`enterWith`/
  `AsyncResource`; gated paths below test it.
- `AsyncLocalStorage.snapshot()` returns a function that runs its argument in
  the frame captured at snapshot time; `AsyncLocalStorage.bind(fn)` is
  `snapshot` applied to `fn`.

This matches Node 24's `AsyncContextFrame` and the TC39 AsyncContext proposal,
so the observable behaviour (including `EventEmitter` listeners running in the
emitter's context, and `unhandledRejection` running in the rejecting
context) follows Node 24, not the older `async_hooks`-based ALS.

### Capture and restore points

The census of async entry points (2026-10-20) found no single "make callback"
helper, but nearly every continuation funnels through a handful of places.

Tier 1, core (covers awaits, promises, queues, timers and every promise-based
native API):

| Continuation | Capture | Restore | Storage |
|---|---|---|---|
| `await` (interpreter, JIT, inline async, async generators) | `js_promise_await_coroutine` (`src/modules/promise.c:330`), the only await registration path | `resume_coroutine_if_suspended` (`src/sugar.c`), next to `coroutine_activate`/`coroutine_deactivate` | new `ant_value_t async_frame` on `coroutine_t` (`include/sugar.h:18`); coroutines are calloc'd and not size-asserted |
| `.then` / `.catch` / `.finally`, `Ant_Promise_Observe`, `js_promise_then` | `builtin_promise_then` and the C helpers, only when `async_frame_used` and the frame is non-empty | `js_process_promise_handlers` (`src/modules/promise.c:402`) around the callback call | an internal slot `SLOT_ASYNC_FRAME` on the derived promise (`nextPromise`); extra slots live in the object sidecar, so promises without a captured frame are unchanged |
| `queueMicrotask`, `process.nextTick`, thenable jobs, `stream_schedule_microtask`, deferred dns/zlib/tls jobs | `queue_job` / `queue_microtask*` / `queue_next_tick*` (`src/modules/timer.c:805-919`) | `process_microtask_entry` (`timer.c:951`) | `ant_value_t async_frame` on `microtask_entry_t` (`timer.c:63`; short-lived malloc'd entries, also used by the per-realm queue in `ant_realm_t.microtasks`) |
| `setTimeout` / `setInterval` | `js_set_timeout` / `js_set_interval` (`timer.c:425/460`) | `timer_callback` (`timer.c:365`) | field on `timer_entry_t` (`timer.c:27`) |
| `setImmediate` | `js_set_immediate` (`timer.c:506`) | `process_immediates` (`timer.c:1154`) | field on `immediate_entry_t` (`timer.c:76`) |
| timers/promises, fs/promises and the fs callback API (wrapper over `Ant_Promise_Observe`, `fs.c:5816`), dns.promises, fetch, WHATWG streams, `Ant.serve` results, readline/promises, `Atomics.waitAsync`, navigator.locks, rpc client, isolates | already promise reactions | via the `.then` row | none |
| `ant:cron` | job creation | `cron_timer_callback` (`src/modules/cron.c:889`) | field on `cron_job_t` |

`MT_AWAIT_RESUME*` and `PROMISE_ALL_ELEMENT` handlers do not call user code
directly; the await rows cover them. Sync generators are untouched (their
`next()` runs in the caller's frame, as in Node). `settle_tail_await` resolves
without resuming, so it needs no swap.

Tier 2, resources (Node runs these callbacks in the frame active when the
resource was created):

| Source | Swap site | Frame stored on |
|---|---|---|
| net sockets/servers (`src/modules/net.c`) | `net_emit` (:133; 13 sites), sync write/end callbacks (:835, :861), `listening` via nextTick (:1046) | socket / server struct |
| tls (`src/modules/tls.c`) | `tls_emit` (:133; 16 sites), `tls_socket_write_cb` (:486) | tls socket struct |
| child_process (`src/modules/child_process.c`) | `emit_event` (:158), `on_child_write_done` (:555), `child_stream_call_callback` (:767), exec close listener (:1496, :1569) | child struct |
| dns callback API (`src/modules/dns/request.c`) | `dns_request_deliver` (:45) | `dns_request_t` |
| fs native-callback ops (`src/modules/fs.c`) | `fs_call_callback` (:232; fsync, read and similar) | `fs_request_s` |
| fs.watch / watchFile / fs streams | `fs_watcher_emit_change/error` (:997/:1006), `fs_watcher_invoke_watchfile_stats` (:1025), `fs_stream_callback` (:300) | watcher / stream state |
| worker_threads (`src/modules/worker/*.c`) | `worker_parent_wake` (core.c:469), `node_worker_emit` (node.c:20), web.c:19, `mailbox_wake` (messaging.c:186), `worker_stdin_call` (stdio.c:100) | worker struct |
| MessagePort / BroadcastChannel (`src/modules/message_port.c`) | `port_dispatch` (:293), `broadcast_async_cb` (:785) | port / channel |
| process signals, stdin, SIGWINCH (`src/modules/process.c`) | `emit_process_event` (:327), stdin (:773-809), :875 | frame at listener registration is not Node semantics; use the root frame (Node runs signal handlers in the empty context) |
| readline (`src/modules/readline.c`) | `emit_event` (:428), `on_sigint` (:577) | interface struct |
| tty | `stream_readable_push` paths (:127, :177) | stream |
| WebSocket / EventSource | `websocket_emit_*` (:254/:287/:311), `eventsource_emit` (:70) | socket / source struct |
| AbortSignal.timeout | `abort_timeout_fire_cb` (`src/modules/abort.c:257`) | timeout record |
| navigator.locks, rpc server | `lock_run_callback` (`navigator.c:392`), `rpc_server_async_cb` (`rpc.c:583`) | request / route |
| N-API | `napi_make_callback` (`src/napi/async.c:372`) honours `async_context`; `napi_async_init` captures the frame; tsfn `call_js_cb` (:35) and `napi_async_work_after_cb` (:91) restore the frame captured at creation | `napi_async_context` struct, tsfn, work |
| ffi callbacks | `src/modules/ffi.c:897` | root frame (foreign threads have no JS context) |

Each module gets one helper (`ant_async_frame_enter/leave`, inline, two loads
and two stores) around its existing emit helper rather than per call site.
`EventEmitter` and `EventTarget` dispatch need no change: listeners run in the
dispatcher's frame, which is Node's behaviour. `eventemitter_call_listener_inline`
calls closures directly (bypassing `sv_vm_call`), which is fine because the
swap happens in the module helper outside it.

zlib's `zlib_async_fn` and the crypto callbacks run synchronously today, so
they inherit the caller's frame with no work.

### AsyncResource

- Constructor captures `js->async_frame` (and, with Tier 3, assigns an
  `asyncId` / `triggerAsyncId`).
- `runInAsyncScope(fn, thisArg, ...args)` installs the captured frame, calls,
  restores.
- `AsyncResource.bind(fn, type, thisArg)` and `resource.bind(fn, thisArg)`
  return functions that run in the captured frame; fix the current throw (the
  stub constructs the class without `new`).
- `emitDestroy()` calls Tier 3 `destroy` hooks when enabled.
- `events.EventEmitterAsyncResource` on top of it.

### async ids (Tier 3)

- `executionAsyncId()` / `triggerAsyncId()` / `executionAsyncResource()`:
  keep `js->async_exec_id` / `js->async_trigger_id` swapped at the same points
  as the frame. Ids are assigned lazily: resources only get one when a hook is
  enabled or an id is requested (`resource.asyncId()`), from a per-isolate
  64-bit counter. Promises can reuse the existing `promise_id`
  (`ant_promise_state_t`) and `trigger_parent` without new fields.
- Until Tier 3 lands, return stable, documented approximations (top-level
  execution 1, resources 0) as today.

### createHook (Tier 3, opt-in)

- `createHook({ init, before, after, destroy, promiseResolve })` with
  `enable()`/`disable()`; `js->async_hooks_enabled` gates every hook path, so
  the cost is zero while no hook is enabled.
- `init` at resource creation (the Tier 1/2 capture points), `before`/`after`
  around the restore points, `promiseResolve` in `js_resolve_promise`.
- `destroy` for promises needs a finalizer per tracked promise; only register
  it while a hook with `destroy` is enabled. Resource structs call `destroy`
  on close.
- Node marks `createHook`, `AsyncHook` and `executionAsyncResource` as
  discouraged; implement them last and keep them off the fast paths entirely.

## Phases

1. Frames and `AsyncLocalStorage` API (`run`, `exit`, `enterWith`,
   `getStore`, `disable`, `snapshot`, `bind`); Tier 1 capture points;
   `AsyncResource` (`runInAsyncScope`, `bind`, fixed). Measure each capture
   point against the baselines before moving on.
2. Tier 2 resources, one module at a time, each with a Node-parity test
   (server request context across sockets, child process events, worker
   messages, readline, fs.watch).
3. N-API `async_context` and threadsafe functions.
4. Async ids and `executionAsyncResource`.
5. `createHook` and promise hooks behind `async_hooks_enabled`.

## Performance Budget and Measurement

- Instructions retired per op (`/usr/bin/time -l`, min of 3, startup
  subtracted) for `await`, `.then`, `queueMicrotask`, `setImmediate`,
  `setTimeout`, and a socket echo, compared to the baselines above; no-ALS
  regressions must stay under 1% per path, and bench-v8 must be unchanged.
- With one active storage: report the per-path overhead (expected: one slot
  write per `.then`, one frame copy per `run`) and compare with Node 22/24 on
  the same probes.
- `ant_promise_state_t` size assertion unchanged; check `sizeof(coroutine_t)`
  and `microtask_entry_t` growth in the plan update.

## Validation

- Node-parity tests (`node:assert`, pass on Node 24 and Ant): propagation
  through every Tier 1 row, nested `run`, `exit`, `enterWith` inside async
  functions, `snapshot`/`bind`, `AsyncResource` scope and bind, thenables,
  `Promise.all`/`race`/`any`, async generators, unhandled rejections, frames
  across `vm` realms, and isolation between workers.
- Per Tier 2 module: callback context equals the context at resource
  creation.
- GC: frames captured in queues, timers, coroutines and promise slots stay
  alive; ASan and `ANT_GC_STRESS` runs; TSan for worker paths.
- Spec suite, harness, and the full `tests/test_*` comparison against the
  previous binary.

## Open Questions

- Node 22 vs 24 differences (22 uses `async_hooks`-backed ALS by default; 24
  uses AsyncContextFrame): target 24 and note where 22 differs in tests.
- Whether `enterWith` should leak into the caller after an `await` in the
  same async function (Node 24: it does for the remainder of that function's
  continuations only).
- Whether to expose the TC39 `AsyncContext.Variable` / `Snapshot` globals on
  the same frames once the proposal stabilises.
