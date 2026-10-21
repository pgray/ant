# Shared-Memory Threads

Status: active
Last reviewed: 2026-10-19
Owner: theMackabu

## Goal

Give Ant programs shared-memory parallelism with a `new Thread(fn)` API in
the spirit of WebKit's "Concurrent JavaScript: It can work!" (2017), built on
the isolate-per-thread runtime instead of one shared heap. Programs share
fixed-layout objects, arrays, mutexes and conditions by reference across
threads and workers. Code that never touches shared objects must not get
slower: no new checks on ordinary property, call or allocation fast paths.

## Background and Direction

The WebKit design shares the whole heap: any object can be reshaped from any
thread. That needs transition-thread-locality tags in pointer bits, segmented
butterflies, buffered inline-cache updates and a multi-mutator GC; WebKit
estimated ~7x for transitions on shared objects and ~2x for their other
accesses, and never shipped it (bug 174276). In Ant it would reverse the
isolate ownership work in `worker-threads-isolates.md`:

- `ant_t *js` appears ~5,300 times in 212 files and mixes per-thread state
  (`vm`, `this_val`, `current_func`, exception state) with heap state
  (object list, arenas, shapes, intern table, GC).
- `ant_object_t` holds `shape` and `overflow_prop` as separate words, so a
  transition is two non-atomic stores; arrays reallocate `u.array.data`; every
  allocation links into the isolate's `objects` list.
- The GC is stop-the-world with one mutator; ICs, feedback sidecars, MIR code,
  rope flattening and interning all mutate without synchronization.

Fixed-layout shared objects remove the hardest part (concurrent reshaping).
Ant is well placed for them because every isolate lives in one pointer cage:
a NaN-boxed reference to a shared region means the same thing in every
isolate, with no handle translation.

The reference semantics are V8's experimental shared structs (Node 22,
`node --harmony-struct`), checked on 2026-10-19:

- `new SharedStructType(['x', 'y'])` returns a constructor; instances have a
  `null` prototype, are sealed and non-extensible; adding a field throws
  `TypeError: Cannot add property z, object has fixed layout`.
- Fields accept `undefined`, `null`, booleans, numbers, strings and shared
  objects; BigInt, Symbol, ordinary objects, arrays, functions and even
  `SharedArrayBuffer` throw `TypeError: ... cannot be shared`.
- `new SharedArray(n)` is fixed length, filled with `undefined`; out-of-range
  stores throw the fixed-layout error.
- `Atomics.Mutex.lock(m, fn)` returns `fn()`; also `tryLock`,
  `lockWithTimeout`, `isMutex`. `Atomics.Condition.wait(c, m[, timeout])`,
  `notify(c[, count])`, `isCondition`.
- Shared objects keep identity through `workerData` and `postMessage`.

## API

```js
import { Thread, Lock, Condition, ThreadLocal } from 'ant:thread';

const Counter = new SharedStructType(['value', 'lock']);
const counter = new Counter();
counter.value = 0;
counter.lock = new Lock();

const workers = Array.from({ length: 4 }, () =>
  new Thread((c, n) => {
    for (let i = 0; i < n; i++) c.lock.hold(() => { c.value++; });
    return c.value;
  }, counter, 1e5));

await Promise.all(workers.map(t => t.asyncJoin()));
```

- `new Thread(fn, ...args)` runs `fn(...args)` on a new isolate thread.
  `fn` must be closure-free (no captured upvalues, not bound, not native);
  the constructor throws `TypeError` otherwise. Its source is re-evaluated in
  the child, like a worker `eval` entry. Shared objects in `args` pass by
  reference; everything else is structured-cloned.
- `thread.join()` blocks and returns the result or rethrows; allowed on any
  thread, matching `Atomics.wait` on Node's main thread. `thread.asyncJoin()`
  returns a promise. `Thread.current` is the calling thread's `Thread` (the
  main thread has one too).
- `Lock` and `Condition` are WebKit's names over `Atomics.Mutex` and
  `Atomics.Condition`: `lock.hold(fn)`, `lock.asyncHold()`,
  `cond.wait(lock)`, `cond.asyncWait(lock)`, `cond.notify()`, `cond.notifyAll()`.
- `ThreadLocal` exposes `value` per isolate. In the isolate model every global
  is already thread-local; it exists for API parity.
- `Thread.restrict` from the WebKit design is not needed: ordinary objects
  can never cross threads.

## Phases

Each phase lands separately with tests, TSan on the thread tests and the GC
gauntlet (fixture, apps2, v8fixed) showing no instruction-count regression.

### 1. Shared synchronization objects

- `Atomics.Mutex` and `Atomics.Condition` as refcounted native objects living
  outside any isolate, wire-serialized by reference like
  `SharedArrayBuffer` blocks, so they work with `Worker`, `worker_threads` and
  later `Thread`.
- Blocking waits reuse the futex-style wait queue in `src/modules/atomics.c`;
  async waits reuse the `waitAsync` path (`uv_async_t` per waiter). Waiting
  stays interruptible by `terminate()` through `atomics_wake_isolate`.
- `Lock`/`Condition` wrappers and the `ant:thread` module skeleton.
- Acceptance: Node `--harmony-struct` parity for `lock`, `tryLock`,
  `lockWithTimeout`, `wait`, `notify` across workers; a contention test.

### 2. `new Thread(fn)` on isolates

- A third front end in `src/modules/worker/` (`thread.c`) on `worker_ops_t`:
  entry is the function source plus a wire of the arguments; the result or
  error comes back through the existing event queue.
- Closure-free check: `sv_closure_t` with no upvalues, no `bound_argc`, not a
  cfunc; class constructors and generators rejected for now.
- Cost note: each `Thread` creates an isolate (milliseconds). A follow-up can
  pool idle isolates; measure before deciding.
- Acceptance: join/asyncJoin/error propagation/terminate tests; TSan.

### 3. Shared arena and `SharedStructType`

- A process-wide fixed arena in the cage for shared objects, with its own
  arena id in the owner table so `fixed_arena_holds_offset` keeps isolates'
  marking from treating shared offsets as their own. Allocation is a
  lock-protected bump/free-list first; per-thread allocation buffers only if
  profiles demand it.
- `SharedStructType(fields)` creates an immutable layout (field names interned
  into a shared, immutable name table); instances store a layout pointer and
  `n` 64-bit field slots. Loads and stores are relaxed 64-bit atomics, which
  matches the SharedArrayBuffer memory model.
- Storable values follow V8. Strings are copied into an immutable shared
  string region on store (flattened; ropes never enter shared space), so a
  shared field never points into an isolate heap and no cross-heap write
  barrier is needed.
- Property access from JS goes through exotic ops on the struct object
  (`ANT_EXOTIC` path), so ordinary objects keep their fast paths untouched.
- GC: isolates treat shared offsets as leaves. A shared collection requests a
  global safepoint through the existing `interrupt_pending` mechanism, each
  isolate reports the shared offsets it reaches from its roots and heap, then
  one thread marks and sweeps the shared arena and releases the isolates.
  Triggered by shared-allocation volume, never by an isolate's own GC.
- Acceptance: Node `--harmony-struct` parity on the semantics listed above;
  identity across `postMessage`; a GC stress test with threads holding and
  dropping references; TSan and ASan.

### 4. `SharedArray`

- Fixed-length shared element storage in the same arena with the same value
  rules; `length` immutable, out-of-range stores throw.
- `Atomics.load/store/exchange/compareExchange/add` extended to struct fields
  and `SharedArray` elements (WebKit's object-plus-key `Atomics` idea), with
  `Atomics.wait/notify` on them for lock-free algorithms.

### 5. JIT fast paths

- Struct layouts are immutable, so a property IC keyed by layout pointer
  resolves to a fixed slot offset forever; the template JIT and opt tier emit
  a layout compare plus one load/store. No transitions, no watchpoints.
- Acceptance: a microbenchmark of field updates under `Lock` and a lock-free
  `Atomics.add` counter; no change on the non-shared gauntlet.

### 6. Research: shared-heap `Thread` (closures across threads)

Only if phases 1–5 show demand for capturing closures. Go/no-go needs a
prototype with at most 1% serial overhead on the gauntlet. Prerequisites:

- Split `ant_t` into a heap (`ant_heap_t`: arenas, object list, shapes,
  intern table, GC) and a per-thread context (`vm`, exception, call state).
- Multi-mutator GC: per-thread allocation buffers in the fixed arenas,
  safepoints at back-edges and allocation, conservative scanning of every
  thread stack.
- Object model: owner-thread tags in the high bits of `overflow_prop`
  (TTL), a segmented overflow layout for objects reshaped by a non-owner, CAS
  on array storage growth, dictionary-mode reads under a shape lock.
- Per-code-unit "ran on one thread" bits, buffered IC/feedback updates flushed
  at safepoints, and thread-safe rope flattening and interning.

## Decisions

- Build on isolates plus shared objects, not a shared heap; keep the WebKit
  `Thread`/`Lock`/`Condition` API on top.
- Follow V8's experimental shared-struct semantics where they exist so code
  can move between Ant and Node `--harmony-struct`.
- Shared objects never reference isolate heaps; that is what keeps ordinary
  code free of new barriers and checks.

## Open Questions

- `ant:thread` module (recommended, avoids a non-standard global) or a global
  `Thread`.
- Whether `SharedStructType` and `SharedArray` stay global like V8 or move
  under `ant:thread` until TC39 settles the `shared struct` syntax.
- Isolate pooling for `Thread` startup cost.

## Risks

- Shared-space GC pauses stop every isolate; keep collections rare and measure
  pause time with many threads.
- Struct field access through exotic ops is slower than ordinary objects until
  phase 5.
- String copying on store costs memory for large strings; revisit with shared
  string interning if profiles show it.

## Validation Plan

- Per phase: focused tests in `tests/`, parity runs against
  `node --harmony-struct` where the API exists, TSan for every thread test,
  ASan for the shared arena and GC, and the GC gauntlet for regressions.
