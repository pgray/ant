# Compile Module Tracer

Status: active
Last reviewed: 2026-10-19
Owner: theMackabu

## Goal

`ant compile` must embed every module a program can load without running it.
The previous tracer (`src/esm/trace.c`) matched a fixed set of syntax shapes
(`require(lit)`, `import(lit)`, `new URL(lit, import.meta.url)`) over a flat
file-wide table of `const` strings, so computed paths, aliased loaders and
workers were missed or misattributed.

## Design

`src/esm/trace/` is a small abstract interpreter split by concern:

- `graph.c`: public API, module table, bundle keys, edges, warnings, per-module
  read/strip/parse and dependency resolution (unchanged behaviour).
- `value.c`: the value domain and an arena. A string value is a set of up to 8
  alternatives; an alternative may contain holes (`TRACE_HOLE`) for unknown
  parts, so `` `./locales/${x}.json` `` is a pattern rather than "unknown". A
  `partial` flag marks sets that may hold other values; concatenation turns it
  into a hole. Paths are normalized like `path.posix`.
- `eval.c`: lexical scopes and expression evaluation (literals, templates, `+`,
  ternaries, `&&`/`||`/`??`, equality, `typeof`, `!`, member access, calls of
  known functions, `new URL`, `await`, dynamic `import()` of builtins).
- `builtins.h`: X-macro tables of known modules (`path`, `url`, `module`,
  `worker_threads`, `child_process`, `process`), their members, globals
  (`require`, `URL`, `Worker`, `process`) and which functions are loader
  sinks. Supporting a new loader API is a table entry.
- `scan.c`: walks the AST with real scopes and hoisting, prunes statically
  decided branches (`if`, ternaries, `&&`/`||`), binds `const`, never-assigned
  `let`/`var`, destructuring, imports and `for (const x of [..])`, and turns sink
  calls into dependencies.
- `expand.c`: expands a hole-bearing relative or absolute specifier against the
  file system (webpack-style contexts), skipping `node_modules` and dot entries,
  capped at 256 matches. Extension-less patterns also record the extension-less
  spec so `require('./plugins/' + name)` resolves at runtime.
- `native.c`: native package materialization and the darwin-universal
  dedupe, moved verbatim.

Sinks: `require`, `require.resolve`, `createRequire(...)`, `import()`, static
imports/exports, `worker_threads.Worker`, the global `Worker`,
`child_process.fork` and `new URL(x, import.meta.url)`. Values derived from
`__dirname`, `__filename` or `import.meta` are absolute disk paths; they embed
the target without a bundle edge because the runtime sees the matching
`/$ant/...` key.

## Decisions

- Keep every diagnostic of the old tracer word for word (the native addon test
  counts them) and keep the deferred try/catch warnings suppressed when a module
  contributes a native fallback.
- A binding shadows builtins, so a local `require` parameter is not a sink.
- Warning lines are computed from `src_off` on demand; the parser does not fill
  `line`, so the old tracer always printed `:0`.

## Validation

- `tests/test_compile_basic.cjs`, `tests/test_compile_native_addon.cjs` pass.
- `tests/test_compile_inference.cjs` covers `path.join(__dirname, ..)`,
  ternaries, template patterns, extension-less expansion, non-reassigned `let`,
  both `Worker` kinds, shadowed `require` and pruned branches, running the
  binary after the source tree is deleted.
- Old vs new on `examples/compile`, `examples/npm/hono`, `examples/npm/jiti`,
  esbuild's `main.js` and the compile fixture: identical module sets except
  jiti, where the new tracer also embeds `dist/babel.cjs` loaded through
  `createRequire(import.meta.url)("../dist/babel.cjs")`.

## Follow-ups

- `ant compile --include <file|glob>` for loads no inference can see.
- Object literals and function return values are not modeled.
