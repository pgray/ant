# Optimizing-Tier Object Literal Miscompile

Status: active
Last reviewed: 2026-10-30
Owner: theMackabu

## Problem

Once the optimizing tier compiles a function, object literals it allocates
can come back without their properties. Choosing between two literals with
`?:` can also produce the wrong object. The interpreter and the template JIT
are correct.

```js
function f(v) { if (v === undefined) return { t: 'u' }; return v; }
for (let i = 0; i < 3000; i++) f(i % 2 ? undefined : i);
console.log(JSON.stringify(f(undefined)));  // {} (expected {"t":"u"})
```

```js
function k(a, b, c) {
  const o = (a & 1) ? { x: a, y: b } : { y: b, x: a, z: c };
  return o.x - o.y + (o.z ?? 100);  // odd a takes the even branch's result
}
```

`--jitless` and `ANT_DEBUG=opt:off` print the expected values;
`ANT_DEBUG=opt:force` reproduces the bug.

## Evidence

- Build: clang 19, `-Db_lto=false -Dpgo=disabled -Dtemporal=disabled`, Linux
  x86-64. It has not been checked with the release toolchain, LTO or PGO,
  which is where to start: if those builds are correct, the trigger is in
  how this configuration compiles the JIT, not in the generated code.
- It reproduces at 18ecc93, fcd7c95 and 734dc7f with no other changes, so
  the recent Silver Assembler commits did not introduce it.
- `ANT_DEBUG=dump/opt:graph` shows correct IR before and after optimization
  for `f`: `AllocateObject` with the one-property literal shape, then
  `InitializeNamed`, then `Return`. The fault is therefore in lowering or
  code generation of that pair (inline allocation, shape install, or the
  slot store), not in the graph passes.
- `tests/test_opt_alloc.cjs` fails in default mode (`default: literal`).
- `examples/spec/run.js --all` reports `worker_threads` and `zlib` as
  errors. Both pass their own checks, then the runner reads `.length` of a
  result object the bug emptied. With `opt:off` both pass.
- The differential `jit` family finds it within about 20 cases
  (`node tests/differential/runner.ts --family jit --cases 60 --seed 6 --minimize`),
  and shrinks it to one kernel with one object-literal statement.

## Next steps

1. Run `tests/test_opt_alloc.cjs` and the two repros above on a release
   build (LTO and PGO, the CI toolchain) to tell a configuration trigger from
   a codegen bug.
2. If it reproduces there, bisect with `tests/test_opt_alloc.cjs`. Commits
   that touched literal allocation on the optimizing tier and are worth
   checking first: 5bde025 (constructor objects sized per site), 891bbfd
   (escape analysis), dea54f1 (prototype shapes in field plans).
3. Compare the `dump/opt:graph` assembly for `f` with the template JIT's
   literal path; check the shape written into the allocated object and the
   slot `InitializeNamed` stores to.
4. Once fixed, keep the two repros above as regression tests, and run the
   `jit` family for a few hundred cases on several seeds.
