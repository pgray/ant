# Master performance comparison, October 14

Status: active
Date: 2026-10-14
Owner: theMackabu

Measure clean master `c32c3006` against reconstructed historical A/B/C/F
revisions using the 16 application/allocation workloads and eight bench-v8
cases from the September comparison. The user deleted the preserved artifacts;
rebuild and re-pin every executable rather than presenting rebuilt binaries as
the originals. Restore deterministic fixtures, verify current application hashes,
and regenerate Node output oracles. Use serial mirrored timing and record RSS,
outputs, exact arguments, compiler/profile identities and process snapshots.

Build in isolated worktrees under `/tmp/ant/master-perf-20261014/`, preserving
the user's in-progress fs/child_process changes and workspace build. Reuse
dependency source trees only when their wrap and patch inputs match the target
revision. Artifacts live in `.cache/master-perf-20261014/`.

Report A in the result tables, as requested in the earlier comparison. Treat
different committed profiles/dependency revisions as part of each complete build;
do not attribute a timing ratio to one source change without a separate control.
No runtime optimization edits or installation are in scope.

Master advanced from `801b0f21` to `c32c3006` while preparing the run; the latter
is the revision pinned in `initial.json` and the isolated M checkout. It includes
the newly committed fs/child_process tests; uncommitted implementation changes
remain outside the isolated checkout. No local edits were moved or reverted.
