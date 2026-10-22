# for-of JIT Ineligibility

Status: completed
Last reviewed: 2026-10-22
Owner: theMackabu

## Outcome

Synchronous `for...of` no longer stops its enclosing function from being
compiled. `OP_ITER_NEXT` and `OP_ITER_CLOSE` are JIT-eligible
(`include/silver/opcode.h`, landed in `63e16c6e`, #82), and dense arrays get an
inline `SV_ITER_ARRAY` fast path (`src/jit/emit_iteration.c`) that falls back
to the helper for Map, string and generic iterators.

`ANT_DEBUG="dump/vm:op-warn" ./build/ant examples/jit/generic_iterators.mjs`
reports no ineligible ops. `arrayIter`, `generatorIter` and `userIter` all
compile and OSR.

## Key Decisions

- Lift the function-level veto with helper calls first, then inline the array
  case. Removing the veto was most of the win.
- `OP_ITER_CLOSE` shares the `OP_DESTRUCTURE_CLOSE` path because both do the
  same thing.
- Generic iterators could not be left out of scope, because they use the same
  opcodes. Validation had to cover user `Symbol.iterator` objects and
  generators, which run user JS across the loop back-edge.
- Rejected: copying the stack handling from `OP_DESTRUCTURE_NEXT`. Those
  opcodes run in straight-line code, but `OP_ITER_NEXT` sits under a loop
  back-edge with an OSR entry, so the iterator slots must survive the vstack
  merge at the loop head. An earlier attempt that ignored this miscompiled
  under load (`TypeError: iterator.next is not a function`) and was reverted
  rather than landed partially.

## Validation

`examples/jit/bench_iteration.mjs` on darwin-aarch64:

| loop form             | 2026-07-31 | 2026-10-22 | node v26.5.1 |
| --------------------- | ---------- | ---------- | ------------ |
| `for..of` array       | 26.5 ns    | 3.7 ns     | 1.2 ns       |
| indexed `for`         | 7.1 ns     | 2.0 ns     | 0.8 ns       |
| `for..of`, no read    | 23.6 ns    | 3.3 ns     | 0.4 ns       |
| `for..of` map.values  | 24.2 ns    | 4.5 ns     | 1.1 ns       |

The 2026-07-31 numbers come from a PGO release build. The 2026-10-22 numbers
come from the local worktree build, so compare ratios rather than absolute
times. The `for..of`-to-indexed ratio fell from 3.7x to 1.85x.
`tests/test_jit_for_of.cjs` passes.

Any remaining Game of Life gap is now ordinary performance work, tracked in
[Silver Throughput: bench-v8 and Game of Life](../active/silver-throughput-bench-v8-game-of-life.md).

The full investigation notes are at revision `de92cab0`.

## Follow-ups

- `OP_ITER_GET_VALUE` is still never emitted by the compiler. Only the
  interpreter dispatch label references it. Delete it separately.
- As of 2026-07-31, `Map.get` with string keys re-hashed on every lookup,
  measured at 8-10x Node. Caching a string's hash would need room in the
  16-byte `ant_flat_string_t` header.
