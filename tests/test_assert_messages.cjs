const assert = require('node:assert');

const cases = {
  "ok(0)": () => assert.ok(0),
  "assert(false)": () => assert(false),
  "ok expression": () => assert.ok(1 > 2 && 'x'),
  "ok no args": () => assert.ok(),
  "ok custom": () => assert.ok(false, 'nope'),
  "equal": () => assert.equal(1, 2),
  "equal strings": () => assert.equal('a', 'b'),
  "notEqual": () => assert.notEqual(1, 1),
  "equal custom": () => assert.equal(1, 2, 'custom'),
  "strictEqual": () => assert.strictEqual(1, 2),
  "strictEqual custom": () => assert.strictEqual(1, 2, 'custom'),
  "strictEqual -0": () => assert.strictEqual(0, -0),
  "strictEqual indicator": () => assert.strictEqual('hello world!', 'hello world?'),
  "strictEqual long strings": () => assert.strictEqual('a'.repeat(50), 'a'.repeat(49) + 'b'),
  "strictEqual NaN": () => assert.strictEqual(NaN, 1),
  "strictEqual same structure": () => assert.strictEqual([1], [1]),
  "strictEqual objects": () => assert.strictEqual({ a: 1 }, { a: 2 }),
  "notStrictEqual": () => assert.notStrictEqual(1, 1),
  "notStrictEqual long": () => assert.notStrictEqual('abcdef', 'abcdef'),
  "notStrictEqual object": () => { const o = { x: 1 }; assert.notStrictEqual(o, o); },
  "deepEqual": () => assert.deepEqual({ a: 1 }, { a: 2 }),
  "deepEqual arrays": () => assert.deepEqual([1, 2], [1, 3]),
  "notDeepEqual": () => assert.notDeepEqual(1, 1),
  "notDeepEqual arrays": () => assert.notDeepEqual([1, 2], [1, 2]),
  "deepStrictEqual": () => assert.deepStrictEqual([1], [2]),
  "deepStrictEqual custom": () => assert.deepStrictEqual({ a: 1 }, { a: 2 }, 'custom'),
  "deepStrictEqual object": () => assert.deepStrictEqual({ a: 1, b: 2, c: 3 }, { a: 1, b: 3, c: 3 }),
  "deepStrictEqual sorted keys": () => assert.deepStrictEqual({ b: 1, a: 2 }, { b: 1, a: 3 }),
  "deepStrictEqual nested": () => assert.deepStrictEqual({ a: [1, 2], b: { c: 'x' } }, { a: [1, 3], b: { c: 'y' } }),
  "deepStrictEqual extra line": () => assert.deepStrictEqual([1, 2, 3], [1, 2, 3, 4]),
  "deepStrictEqual skipped lines": () => assert.deepStrictEqual(Array.from({ length: 30 }, (_, i) => i), Array.from({ length: 30 }, (_, i) => i === 15 ? 99 : i)),
  "deepStrictEqual map": () => assert.deepStrictEqual(new Map([[1, 2]]), new Map([[1, 3]])),
  "notDeepStrictEqual": () => assert.notDeepStrictEqual(1, 1),
  "notDeepStrictEqual object": () => assert.notDeepStrictEqual({ a: 1 }, { a: 1 }),
  "match": () => assert.match('abc', /x/),
  "match custom": () => assert.match('abc', /x/, 'custom'),
  "doesNotMatch": () => assert.doesNotMatch('abc', /b/),
  "match non-string": () => assert.match(1, /x/),
  "fail": () => assert.fail(),
  "fail message": () => assert.fail('x'),
  "ctor operator": () => { throw new assert.AssertionError({ actual: 1, expected: 2, operator: 'strictEqual' }); },
  "ctor no operator": () => { throw new assert.AssertionError({ actual: 1, expected: 2 }); },
  "ctor custom diff": () => { throw new assert.AssertionError({ message: 'hi', actual: 1, expected: 2, operator: 'deepStrictEqual' }); },
};

const expected = [
  ["ok(0)", "The expression evaluated to a falsy value:\n\n  assert.ok(0)\n", true, "=="],
  ["assert(false)", "The expression evaluated to a falsy value:\n\n  assert(false)\n", true, "=="],
  ["ok expression", "The expression evaluated to a falsy value:\n\n  assert.ok(1 > 2 && 'x')\n", true, "=="],
  ["ok no args", "No value argument passed to `assert.ok()`", true, "=="],
  ["ok custom", "nope", false, "=="],
  ["equal", "1 == 2", true, "=="],
  ["equal strings", "'a' == 'b'", true, "=="],
  ["notEqual", "1 != 1", true, "!="],
  ["equal custom", "custom", false, "=="],
  ["strictEqual", "Expected values to be strictly equal:\n\n1 !== 2\n", true, "strictEqual"],
  ["strictEqual custom", "custom\n\n1 !== 2\n", false, "strictEqual"],
  ["strictEqual -0", "Expected values to be strictly equal:\n+ actual - expected\n\n+ 0\n- -0\n", true, "strictEqual"],
  ["strictEqual indicator", "Expected values to be strictly equal:\n+ actual - expected\n\n+ 'hello world!'\n- 'hello world?'\n              ^\n", true, "strictEqual"],
  ["strictEqual long strings", "Expected values to be strictly equal:\n+ actual - expected\n\n+ 'aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa'\n- 'aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaab'\n", true, "strictEqual"],
  ["strictEqual NaN", "Expected values to be strictly equal:\n\nNaN !== 1\n", true, "strictEqual"],
  ["strictEqual same structure", "Values have same structure but are not reference-equal:\n\n[\n  1\n]\n", true, "strictEqual"],
  ["strictEqual objects", "Expected \"actual\" to be reference-equal to \"expected\":\n+ actual - expected\n\n  {\n+   a: 1\n-   a: 2\n  }\n", true, "strictEqual"],
  ["notStrictEqual", "Expected \"actual\" to be strictly unequal to: 1", true, "notStrictEqual"],
  ["notStrictEqual long", "Expected \"actual\" to be strictly unequal to:\n\n'abcdef'", true, "notStrictEqual"],
  ["notStrictEqual object", "Expected \"actual\" not to be reference-equal to \"expected\":\n\n{\n  x: 1\n}\n", true, "notStrictEqual"],
  ["deepEqual", "Expected values to be loosely deep-equal:\n\n{\n  a: 1\n}\n\nshould loosely deep-equal\n\n{\n  a: 2\n}", true, "deepEqual"],
  ["deepEqual arrays", "Expected values to be loosely deep-equal:\n\n[\n  1,\n  2\n]\n\nshould loosely deep-equal\n\n[\n  1,\n  3\n]", true, "deepEqual"],
  ["notDeepEqual", "Expected \"actual\" not to be loosely deep-equal to:\n\n1", true, "notDeepEqual"],
  ["notDeepEqual arrays", "Expected \"actual\" not to be loosely deep-equal to:\n\n[\n  1,\n  2\n]", true, "notDeepEqual"],
  ["deepStrictEqual", "Expected values to be strictly deep-equal:\n+ actual - expected\n\n  [\n+   1\n-   2\n  ]\n", true, "deepStrictEqual"],
  ["deepStrictEqual custom", "custom\n+ actual - expected\n\n  {\n+   a: 1\n-   a: 2\n  }\n", false, "deepStrictEqual"],
  ["deepStrictEqual object", "Expected values to be strictly deep-equal:\n+ actual - expected\n\n  {\n    a: 1,\n+   b: 2,\n-   b: 3,\n    c: 3\n  }\n", true, "deepStrictEqual"],
  ["deepStrictEqual sorted keys", "Expected values to be strictly deep-equal:\n+ actual - expected\n\n  {\n+   a: 2,\n-   a: 3,\n    b: 1\n  }\n", true, "deepStrictEqual"],
  ["deepStrictEqual nested", "Expected values to be strictly deep-equal:\n+ actual - expected\n\n  {\n    a: [\n      1,\n+     2\n-     3\n    ],\n    b: {\n+     c: 'x'\n-     c: 'y'\n    }\n  }\n", true, "deepStrictEqual"],
  ["deepStrictEqual extra line", "Expected values to be strictly deep-equal:\n+ actual - expected\n\n  [\n    1,\n    2,\n    3,\n-   4\n  ]\n", true, "deepStrictEqual"],
  ["deepStrictEqual skipped lines", "Expected values to be strictly deep-equal:\n+ actual - expected\n... Skipped lines\n\n  [\n    0,\n    1,\n    2,\n    3,\n...\n    14,\n+   15,\n-   99,\n    16,\n    17,\n    18,\n    19,\n    20,\n", true, "deepStrictEqual"],
  ["deepStrictEqual map", "Expected values to be strictly deep-equal:\n+ actual - expected\n\n  Map(1) {\n+   1 => 2\n-   1 => 3\n  }\n", true, "deepStrictEqual"],
  ["notDeepStrictEqual", "Expected \"actual\" not to be strictly deep-equal to: 1", true, "notDeepStrictEqual"],
  ["notDeepStrictEqual object", "Expected \"actual\" not to be strictly deep-equal to:\n\n{\n  a: 1\n}\n", true, "notDeepStrictEqual"],
  ["match", "The input did not match the regular expression /x/. Input:\n\n'abc'\n", true, "match"],
  ["match custom", "custom", false, "match"],
  ["doesNotMatch", "The input was expected to not match the regular expression /b/. Input:\n\n'abc'\n", true, "doesNotMatch"],
  ["match non-string", "The \"string\" argument must be of type string. Received type number (1)", true, "match"],
  ["fail", "Failed", true, "fail"],
  ["fail message", "x", false, "fail"],
  ["ctor operator", "Expected values to be strictly equal:\n\n1 !== 2\n", true, "strictEqual"],
  ["ctor no operator", "1 undefined 2", true, undefined],
  ["ctor custom diff", "hi\n\n1 !== 2\n", false, "deepStrictEqual"],
];

for (const [label, message, generated, operator] of expected) {
  let error;
  try { cases[label](); } catch (e) { error = e; }
  assert.ok(error instanceof assert.AssertionError, label);
  assert.strictEqual(error.message, message, label);
  assert.strictEqual(error.generatedMessage, generated, label);
  assert.strictEqual(error.operator, operator, label);
  assert.strictEqual(error.code, 'ERR_ASSERTION', label);
}

function hot(v) { assert.ok(v); }
for (let i = 0; i < 20000; i++) hot(1);
assert.throws(() => hot(0), { message: 'The expression evaluated to a falsy value:\n\n  assert.ok(v)\n' });

assert.throws(() => assert.match('a', 'x'), { code: 'ERR_INVALID_ARG_TYPE' });
assert.throws(() => new assert.AssertionError(), { code: 'ERR_INVALID_ARG_TYPE' });
assert.throws(() => assert.ok(null, new TypeError('boom')), { name: 'TypeError', message: 'boom' });

console.log('assert messages OK');
