# Fuzz and Property Testing

Status: active
Last reviewed: 2026-10-30
Owner: theMackabu

## Goal

Find bugs that example-based tests miss by checking properties over generated
inputs: libFuzzer targets for C parsers and codecs, and Node-oracle and
JIT-tier families for the [differential runner](node-ant-differential-runner.md).
See [testing.md](../../repo/testing.md) for when to run them.

## Decisions

- Targets check properties (split-invariance, round-trips, agreement between
  two implementations, bounds), so they catch wrong answers, not only
  crashes. Each target documents its input layout and properties at the top.
- Every target links either libFuzzer (`-Dfuzz=enabled`, clang) or
  `tests/fuzz/driver.c`, which replays the corpus plus 500 seeded mutations.
  The replay keeps the targets running as ordinary meson tests (`--suite
  fuzz`) under any compiler, with no new dependencies.
- Inputs are copied into exactly sized buffers so ASan sees reads past a
  length the caller passed, even when real callers pass NUL-terminated or
  larger buffers.
- `js` parses and compiles but never runs code: running would need timeouts
  and would mostly fuzz built-ins already covered elsewhere. It runs without
  LeakSanitizer, which does not scan the GC heap and reports storage owned by
  GC objects as leaked.
- The differential runner is TypeScript run directly by Node, with
  fast-check generating and shrinking params, so a mismatch reduces to a
  small repro (for example a single statement and short expression in a JIT
  kernel). Probes encode special values as strings rather than object
  literals: their encoder runs hot, and an optimizing-tier literal bug was
  otherwise reported by every family.
- Node-oracle families record only error names. Messages, `stack` text and
  `Number.prototype.toString(radix)` of fractions are engine-specific.
- The `jit` family compares five Ant variants with Node instead of with each
  other, so a wrong answer on every tier is still caught, and a mismatch on
  the default and `opt:*` variants alone points at the optimizing tier.
  Kernels avoid `Math` functions the spec lets engines approximate.
- CI does not run any of this yet (requested scope). A follow-up can add a
  clang job with `meson test --suite fuzz` under ASan and UBSan, and a short
  time-boxed `maid fuzz` per target.

## Fixed

Each fix has a regression test or seed.

- Chunked trailers were merged into the request headers, so a trailer `Host`
  rewrote `req.url` in `Ant.serve` (`fuzz_http1`). The parser now drops trailer
  fields (`tests/test_http_protocols.c`).
- Octal escape decoding read up to two bytes past the literal
  (`src/escape.c`, `fuzz_text`).
- RegExp translation, where patterns failed to compile in PCRE2 and then never
  matched, or matched the wrong thing (`fuzz_regexp`,
  `tests/test_regexp_annex_b_escapes.cjs`):
  - `\N` past the last group was not a legacy octal escape (`/a\2/`);
  - surrogate pair escapes never matched (`/\uD83D\uDE00/`);
  - without `u` or `v`, `\p{...}` and `\u{...}` were read as a property and a
    code point instead of identity escapes;
  - `[` nested in a non-`v` class, `[:`, `[.` and `[=` (POSIX syntax only in
    PCRE2), `\B` in a class, and `{,n}` (a quantifier only in PCRE2);
    identity escapes of punctuation now keep their backslash.

## Open findings

The runs behind these were limited to this checkpoint's build: clang 19,
`-Db_lto=false -Dpgo=disabled -Dtemporal=disabled`, Linux x86-64.

- Optimizing tier: a hot function returning an object literal returns `{}`,
  and choosing between two literals with `?:` yields the wrong one. For
  example, `function f(v) { if (v === undefined) return { t: 'u' }; return v; }`
  called 3000 times returns `{}`. `opt:off` and `--jitless` are correct, and
  `tests/test_opt_alloc.cjs` fails in this build. Reproduced at 18ecc93 and
  its two parents with no other changes; not yet checked with the release
  toolchain or LTO and PGO enabled. Through the probe prelude, this bug also
  shows up as `{}` results in the `string` and `number` families.
- `Number.prototype.toString(radix)` is wrong for integers of 2^53 and above:
  `(1e21).toString(16)` gives `".0000000000000000"`.
- `JSON.stringify` with a replacer array skips inherited properties, and drops
  keys containing NUL when the object also has an integer key:
  `{ 'a\0b': 5, 1: 1 }` gives `{"1":1}`.
- `new URL('http://xn--/')` parses; Node rejects the invalid Punycode label.
- `parseInt` does not skip Unicode whitespace (`parseInt('\u00a012')` is
  `NaN`), and treats a `NaN` radix as invalid instead of as 10.
- RegExp without `u` matches code points rather than code units:
  `'😀'.replace(/(?:)/g, '-')` gives `-😀-`; Node gives three dashes around
  the two surrogates.
- `Object.keys` of a `Readable` lists only `_read` (see
  [stream-property-surface.md](stream-property-surface.md)).
- `structuredClone` turns `Object(1n)` into a plain object, truncates property
  keys at an embedded NUL, and fills array holes with `undefined`.
- `URLSearchParams` turns a lone surrogate into three U+FFFD instead of one.
- `btoa` throws `TypeError` instead of a `DOMException` named
  `InvalidCharacterError`.
- `String.prototype.normalize` returns a string containing a lone surrogate
  unchanged instead of normalizing the rest of it.
- RegExp: lone surrogate escapes cannot match (PCRE2's UTF mode has no
  surrogates); `v`-flag set operations whose operand is itself a class are
  not translated (`/[\p{L}--[a--z]]/v`); group names with `$` or escapes
  fail to compile, so `/(?<a$>x)/` never matches. `fuzz_regexp` skips all
  three.
- `console.log` of a RegExp exec result prints `groups: undefined` even
  though `match.groups` holds the named groups.
- WebSocket frames with reserved opcodes are ignored instead of failing the
  connection (RFC 6455 section 5.2).
- `js_destroy` does not free property and slot storage that `ant_create`
  allocates (about 7 KB per isolate).
- Vendored libbase64 shifts 1 into the sign bit in CPU feature detection,
  which UBSan reports (`tests/fuzz/ubsan.supp`).

## Validation

At this checkpoint, with the build described under Open findings:

- `meson test --suite fuzz` passes with the replay driver, as do 20,000 to
  50,000 seeded mutations per target.
- Under ASan and UBSan with libFuzzer: `http1` 443k runs, `websocket` 4.5M,
  `numbers` 123k, `base64` 1.1M, `text` 463k (2 minutes each), `js` 333k and
  `regexp` 417k (3 to 4 minutes) after the fixes above, with no remaining
  failures.
- `node tests/differential/runner.test.ts` and `npx tsc -p tests/differential`
  pass. The families ran with 3 to 25 cases per seed; the open findings above
  are what they reported.
- `examples/spec/run.js --all`: `temporal` fails because Temporal is disabled
  in this build. `worker_threads` and `zlib` pass their own checks, but the
  runner then fails reading `.length`. Those two also fail on unmodified
  18ecc93 and pass with `ANT_DEBUG=opt:off`, so they come from the
  optimizing-tier finding.
- `meson test`: 49 of the 51 tests that build pass. `gc-uv-segments` and
  `isolate-code-arenas` fail, and `shape-descriptor-oom` and
  `shape-transition-identity` do not link (`js_mkundef` undefined); all four
  behave the same on unmodified 18ecc93 in this build.
