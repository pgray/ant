# Microtask Checkpoint Timing in Nested Native→JS Calls

Status: completed
Last reviewed: 2026-10-22
Owner: theMackabu

## Outcome

A promise rejected inside a native-invoked listener (`abort()`, `emit()`, uv
callbacks) is no longer reported as unhandled before the user code that
attaches its handler in the same tick gets control. Microtasks drain only when
the JS stack is empty, matching Node.

- `sv_vm_maybe_checkpoint_microtasks` (`include/silver/call.h`) returns early
  unless `vm_exec_depth == 0`, so nested `sv_vm_call` returns no longer drain.
- `6cfc01cd` removed the per-listener `process_microtasks` calls from abort
  dispatch (`src/modules/abort.c`), which were draining between listeners.

## Repro

```js
const { on, EventEmitter } = require('events');
const em = new EventEmitter();
const ac = new AbortController();
(async () => {
  const it = on(em, 'x', { signal: ac.signal });
  const p = it.next();
  ac.abort();
  try {
    await p;
  } catch (e) {
    console.log('caught:', e.name);
  }
})();
```

Before the fix Ant printed `Uncaught (in promise) AbortError` and then
`caught: AbortError`. It now prints only `caught: AbortError`, like Node.

## Key Decisions

- Fix checkpoint placement in the VM rather than pre-attaching handlers in
  individual modules, which would hide the symptom per call site.
- Module code must not drain microtasks itself between listener calls; leave
  draining to the outermost checkpoint.

## Validation

- 2026-10-22: the repro, plus `emit` and `forEach` variants, match Node.
- `tests/test_abort_listener_microtask_order.mjs` passes.

The full investigation notes are at revision `de92cab0`.
