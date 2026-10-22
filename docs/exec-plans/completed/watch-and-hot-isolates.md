# Watch and Hot Reload on Isolates

Status: completed
Last reviewed: 2026-10-21
Owner: theMackabu

## Goal

Replace the process-based `--watch` supervisor with in-process isolates, and
add `--hot`, which reloads changed modules in place. Supersedes
[Watch Supervisor](watch-supervisor.md).

## Behavior

- `--watch`: every run is a fresh isolate on the main thread. On a change the
  isolate is terminated (interrupt, also inside busy loops) and destroyed, and
  a new one runs the entry. `process.exit()` and uncaught errors end the run;
  the session then waits for the next change. The working directory is reset
  before each run.
- `--hot`: one isolate for the session. On a change, file-backed modules
  outside `node_modules` are dropped from the module table and `require.cache`
  and the entry is evaluated again. `globalThis`, timers and sockets survive.
  Uncaught errors are printed and do not exit; a syntax error waits for the
  next change. `process.exit()` exits.
- `--hot` servers: `Ant.serve` (and `export default { fetch }`) on the same
  port or unix path as a server from an older reload is reloaded in place (same
  server object, new handler and options). A `net.Server` (and so `node:http`)
  listening on the same port or path as an older one closes the old listener
  first. Its open connections, including client keep-alive connections, are
  served by the old handler until they close.
- Ctrl-C or SIGTERM ends the session with `128 + signal`. The program's own
  signal listeners get one loop turn, then the run is interrupted after at
  most 500 ms. A second signal exits immediately.
- The screen is cleared before each restart or reload unless
  `--no-clear-screen` is given.

## Design

- `src/watch.c` owns an `ant_watch_t` session: a watcher thread with its own
  libuv loop, directory `uv_fs_event` watches (so atomic rename saves are
  seen), a 75 ms debounce, and SIGINT/SIGTERM handlers.
- The file graph comes from the loader: `esm_read_file` reports each file it
  reads (`ant_watch_track`) with a stat taken at read time. The watcher
  rescans 250 ms after adding files, so an edit made before the directory watch
  is live is not lost. `node_modules` is not watched.
- `js->watch` points at the session while an isolate is attached. The watcher
  wakes the isolate through a `uv_async_t` on its loop. Restarts set
  `interrupt_pending` and `terminating` like worker termination; the JIT is
  armed for interrupts when attaching. Hot reloads set `js->loop_yield`, which
  makes `js_run_event_loop` (including the nested loop of a top-level
  `await`) return, so the reload runs at top level in
  `ant_entry_files_hot`, where no C frame holds a module record.
- `process_exit_with` and the uncaught-exception exits route to
  `ant_watch_exit` when supervised, as they route to `ant_worker_exit` in a
  worker.
- Servers and `net` listeners record the reload generation
  (`ant_watch_generation`, 0 outside `--hot`) to find listeners from older
  reloads.

## Engine fixes found on the way

- `js_is_termination(Ant_Exception_Peek(js))` never matched: `Peek` returns
  the completion, not the thrown object. `js_termination_pending` checks the
  value. `print_uncaught_throw`, `process_report_uncaught_exception` and the
  worker messaging path use it, so terminations are no longer printed as
  `Script execution interrupted`.
- `ant_inspector_stop` was never called; the session calls it before
  destroying each isolate so the next run can start the inspector again.

## Validation

- `tests/test_watch_isolate.cjs`: busy-loop restart, `process.exit` then
  restart in a fresh isolate, atomic rename save, hot state with a syntax
  error and fix, `Ant.serve` reload in place, and `net` listener takeover.
  Passed 5 consecutive runs.
- `tests/test_watch_supervisor.cjs` passes unchanged.
- Spec suite clean. Focused loader/net/process/server/worker/isolate tests
  show no failures beyond those that also fail on the installed binary.

## Known differences from a process supervisor

- Environment changes (`process.env.X = ...`) persist into the next run.
- A syntax error in an imported file is reported with the importer's path
  (also true outside watch mode).
- Linux and Windows directory watching is untested in this change.
