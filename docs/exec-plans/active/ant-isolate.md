# `ant:isolate` (isolated-vm style)

Status: active (phases 1-3 implemented; follow-ups open)
Last reviewed: 2026-10-19
Owner: theMackabu

## Goal

Run untrusted or separately-budgeted code in its own Ant isolate, with an API
in the style of `isolated-vm`: isolates with a memory limit, contexts,
scripts, references into the isolate, host callbacks, copies, timeouts and
disposal. Values never share memory between isolates; everything crosses
through the wire serializer, with the same rules as `postMessage`
(SharedArrayBuffer shared).

## API

```js
import { Isolate, Context, Script, Reference, Callback, ExternalCopy, copy } from 'ant:isolate';

const iso = new Isolate({ memoryLimit: 128, thread: 'own', globals: 'bare' });
const ctx = await iso.createContext();
await ctx.global.set('input', copy({ a: 1 }));
await ctx.global.set('log', new Callback((...a) => console.log(...a)));
const script = await iso.compileScript('log(input.a); input.a + 1', { filename: 'job.js' });
await script.run(ctx, { timeout: 50 });            // 2
const fn = await ctx.evalReference('(x) => x * 2');
await fn.apply(undefined, [21]);                   // 42
await iso.getHeapStatistics(); iso.cpuTime; iso.dispose();
```

- `Isolate({ memoryLimit, thread, globals })`
  - `memoryLimit` (MB, >= 8, default 128) sizes the isolate's object arena
    (`ant_create`). Exceeding it rejects the running call with
    `ERR_ISOLATE_OUT_OF_MEMORY` and disposes the isolate.
  - `thread: 'own'` (default) runs the isolate on a dedicated 8 MiB-stack
    thread with its own event loop; `'current'` runs it nested on the
    caller's thread.
  - `globals: 'bare'` (default) gives ECMAScript intrinsics, `Intl`,
    `Temporal`, `WebAssembly` and `console`; dynamic `import()` rejects with
    `ERR_VM_DYNAMIC_IMPORT_CALLBACK_MISSING`. `'ant'` bootstraps the full
    runtime (`process`, timers, `import()`) in the main context; like
    `node:vm` scripts, `require` is not a global there.
- Async and `*Sync` forms: `createContext`, `compileScript`,
  `getHeapStatistics`, `script.run`, `ctx.eval`, `ctx.evalReference`,
  `ref.get`, `ref.set`, `ref.apply`, `ref.copy`. Options: `timeout` (ms),
  `promise` (await a returned promise inside the isolate), `reference`
  (return a `Reference`), `filename`.
- `iso.mainContext` is the isolate's main realm; `createContext()` makes a
  fresh realm. `Context#global` is a `Reference` to the realm's global.
- `Context#release()`, `Script#release()` and `Reference#release()` drop the
  child-side handle; later use throws "... has been released". Finalized
  handles are released the same way.
- `Callback(fn)` passed into the isolate becomes a function there; calling it
  copies the arguments to the host, runs `fn`, and copies the result back.
  With `thread: 'own'` a returned promise is awaited; with `'current'` it is
  an error (the caller's thread cannot wait for its own loop).
- `copy(value)` / `new ExternalCopy(value)` hold one serialized copy;
  `copy()`, `copyInto()` and `release()` follow isolated-vm.
- `cpuTime` / `wallTime` are BigInt nanoseconds spent executing in the
  isolate.
- Timeouts throw `ERR_SCRIPT_EXECUTION_TIMEOUT` and leave the isolate usable.
- `dispose()` rejects every pending call with `ERR_ISOLATE_DISPOSED`. Running
  code is interrupted (uncatchable inside the isolate), including when the
  dispose comes from a Callback the isolate is waiting on.
- `Context`, `Script` and `Reference` constructors throw; handles come from an
  `Isolate`.

## Design

One executor, two transports.

- `src/modules/isolate/guest.c` runs every operation inside the child
  isolate. Contexts are realms (`ant_realm_create`) kept alive by
  `guest_child_mark` until released. Scripts store their source and compile
  lazily per context (cached as handles). References are slots in a GC-rooted
  handle table owned by the child (`js->guest`). Callbacks are native
  functions whose call goes back to the host.
- `own.c`: the child thread runs `js_run_event_loop` with a `uv_async_t` call
  queue. Async calls complete on the host loop through a host `uv_async_t`
  (ref'd only while calls are pending). Sync calls block on the condition
  variable while servicing callback requests, so callbacks during `runSync`
  do not deadlock. Host callback calls carry `taken`/`abandoned` ownership:
  on dispose the child frees calls the host never dequeued and abandons the
  rest to the host, which frees them when the callback (or its promise)
  finishes.
- `current.c`: entering the child switches the stack base/limit (from the
  caller's frame) and `ant_cfunc_intern_realm`; nesting is depth-counted and
  a dispose while nested is deferred to the outermost exit. The child loop is
  driven from the host loop by a `uv_poll` on `uv_backend_fd(child)` plus a
  timer from `uv_backend_timeout`.
- `api.c`: the JS classes, option parsing, value encoding (wire copy,
  reference id, callback id, undefined), and the release queue
  (`{kind, id}`) the child applies at its next call so no child code runs
  inside a host GC.
- `globals: 'bare'` uses `ant_bootstrap_bare` (intrinsics +
  `js_realm_finish_intrinsics` + `console`).
- The `node:vm` timeout watchdog moved to `src/modules/vm_interrupt.c` and is
  shared by `vm` and isolates.

## Engine fixes found on the way

- The interpreter's `OP_OBJECT` / `OP_ARRAY` / `OP_PRIVATE_TOKEN` ignored
  allocation failure and wrote through the error value, looping forever on
  OOM. They now return a completion that dispatch checks (`VM_CHECK_ALLOC`).
  Cost: bench-v8 instructions +0.0%; an object/array literal microbench is
  about +1% instructions with `--jitless` and neutral with the JIT.
- `gc_reclaim_arena` reports failure when less than 1/64 of the arena is free
  after collection (`GC_RECLAIM_MIN_FREE_DIVISOR`), so a full arena raises OOM
  instead of thrashing.
- `import()` with no module loader returns a rejected promise
  (`ERR_VM_DYNAMIC_IMPORT_CALLBACK_MISSING`, Node parity) instead of
  `undefined`.
- Engine errors carry `SLOT_ERROR_BRAND`.

## Validation

- `tests/test_isolate.mjs` runs the shared cases under both transports plus
  transport-specific cases (dispose mid-run and from a callback, late promise
  settle after dispose, re-entrant sync calls, GC while nested, calls from a
  `vm` realm, nested isolates, realm survival after `global.release()`).
  It passes with and without `--jitless`, under ASan and TSan, and 30
  consecutive runs.
- `tests/test_vm_dynamic_import_missing.cjs` passes on Node 22 and Ant.
- `isolated-vm` is a native addon Ant cannot load, so behaviour is checked
  against the API contract above rather than a parity run.

## Follow-ups

- Per-call latency on `thread: 'own'`: a warm `evalSync` round trip is about
  12 us (queue, wake, condition-variable wait). Isolate startup is not worth
  snapshots or pooling: `new Isolate()` + `createContext()` + first eval is
  0.2-0.5 ms and a bare isolate is about 0.4 MB RSS (2026-10-19, release
  build).
- Per-isolate CPU limits.
- `Reference#deref`, `ExternalCopy` transfer lists, `transferIn/Out`
  options, and `Isolate#compileModule`.
- A Callback promise that never settles after dispose keeps its call record
  until the host isolate exits.
