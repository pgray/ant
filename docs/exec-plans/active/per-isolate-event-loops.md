# Per-Isolate Event Loops

Status: active
Last reviewed: 2026-10-14
Owner: theMackabu

## Goal

Every isolate runs its own libuv loop, and `loop->data` leads back to the
isolate. Callbacks that only receive a libuv handle or request can then find
their isolate with `Ant_Loop_Isolate(handle->loop)` instead of a stored
`ant_t *js`. This is also groundwork for running isolates on separate threads.

## Invariants

- `ant_t.loop` is set in `isolate_init` by `Ant_Loop_Init` (`src/loop.c`)
  and released in `js_destroy` by `Ant_Loop_Destroy`.
- The first isolate in a process claims `uv_default_loop()`. Every later
  isolate gets a private `uv_loop_t` (`owns_loop`). Destroying the claiming
  isolate releases the claim, so the next isolate takes the default loop again.
- `js->loop->data == js` while the isolate is alive; it is cleared on destroy.
- Runtime code never calls `uv_default_loop()`. It uses `js->loop`, the loop
  of the handle or request it was called with (`uv_req->loop`,
  `server->loop`, `conn->handle.tcp.loop`), or its state's `js->loop`.
  Spawn specs carry `.loop` from their caller.
- The Zig package manager (`src/pkg/`) still uses the default loop. It runs
  outside any isolate.

## Validation

- `meson test isolate-timers` now asserts distinct loops per isolate and the
  `loop->data` mapping, and drives each isolate's own loop. It had been failing
  since timer setup started using symbols; it now initializes the symbol module.
- Spec suite clean; JS harness 386 pass, 18 fail, all missing npm packages in
  `examples/npm` or the existing `test_hono_adapter` failure, identical on the
  installed binary.
- C tests: 31 of 36 pass. `error-handoffs`, `gc-rope-table-oom`,
  `ic-poly-shape-release`, `arguments-storage` and `jit-inline-eligibility`
  fail with the same assertions on a clean HEAD build. `jit-property-ic` and
  `desktop-error-details` do not compile on HEAD either.

## Isolate Lookups

- Timer entries, fs requests and fs watchers no longer store `ant_t *js`.
  Timer callbacks use `Ant_Loop_Isolate(handle->loop)`; fs requests seed
  `req->uv_req.loop` at creation, so `fs_req_isolate()` works before and after
  submission (queued directory ops, `uv_work_t` walks, synchronous failures);
  watchers read the loop of their initialized event or poll handle.
- WebSocket and EventSource keep `js`: a WebSocket owns either a tlsuv client
  or a server connection, and an EventSource recreates its HTTP request on
  every reconnect, so neither has one handle that lives as long as the state.

## Follow-ups

- Multiple isolates on one thread must each be driven (`uv_run` per loop);
  the reactor only runs its own isolate's loop.
