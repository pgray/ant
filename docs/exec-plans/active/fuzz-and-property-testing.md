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
  - surrogate pair escapes never matched (`/😀/`);
  - without `u` or `v`, `\p{...}` and `\u{...}` were read as a property and a
    code point instead of identity escapes;
  - `[` nested in a non-`v` class, `[:`, `[.` and `[=` (POSIX syntax only in
    PCRE2), `\B` in a class, and `{,n}` (a quantifier only in PCRE2);
    identity escapes of punctuation now keep their backslash.
- WebSocket frames with reserved opcodes were accepted and ignored; they now
  fail the connection (RFC 6455 section 5.2; `tests/test_http_protocols.c`).
- From the differential families (`tests/test_json_property_list.cjs`,
  `tests/test_number_radix_parseint.cjs`, `tests/test_structured_clone_shapes.cjs`,
  `tests/test_url_usv_strings.cjs`, `tests/test_string_normalize_surrogates.cjs`):
  - `JSON.stringify` replacer arrays filtered own keys instead of reading the
    PropertyList with `[[Get]]` in list order, and keys containing NUL were
    truncated or dropped (stringify, `toJSON`, replacers, revivers);
  - `Number.prototype.toString(radix)` was wrong from 2^53 up (undefined
    behavior from 2^63); it now prints V8's shortest round-trip digits;
  - `parseInt` ignored Unicode whitespace and a `NaN` or string radix;
  - `btoa` threw `TypeError` instead of `InvalidCharacterError`;
  - `structuredClone` turned primitive wrappers into plain objects, filled
    array holes, dropped named array properties, and truncated NUL keys;
  - URL and URLSearchParams turned a lone surrogate into three U+FFFD
    instead of one, and decoded `%00` truncated keys;
  - `String.prototype.normalize` skipped strings containing a lone surrogate.
- The standalone shape tests (`shape-descriptor-oom`,
  `shape-transition-identity`) did not link; they do again.

## Open findings

Larger issues are written up where they will be scheduled:

- [Optimizing-tier object literal miscompile](opt-tier-object-literal-miscompile.md):
  hot functions return object literals without their properties. The most
  serious finding; it also breaks `tests/test_opt_alloc.cjs` and the spec
  runner's `worker_threads` and `zlib` reports in this build.
- [tech-debt.md](../tech-debt.md):
  - RegExp: exec-result `groups` is an accessor (so `console.log` shows
    `groups: undefined`), non-`u` matching works on code points, lone
    surrogate escapes cannot match, `v`-flag set operations with class
    operands are not translated, and group names with `$` or escapes fail;
  - isolate teardown leaks the property storage `ant_create` allocates;
  - URL: `xn--` hosts parse with ada 4.0.0 but not in Node 22 (ada 2.9.2);
  - vendored libbase64 UB in CPU feature detection
    (`tests/fuzz/ubsan.supp`);
  - `gc-uv-segments` and `isolate-code-arenas` fail in this build.
- `Object.keys` of a `Readable` lists only `_read`
  ([stream-property-surface.md](stream-property-surface.md)).

The runs were limited to one build: clang 19,
`-Db_lto=false -Dpgo=disabled -Dtemporal=disabled`, Linux x86-64.

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
