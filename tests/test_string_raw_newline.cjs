const assert = require('node:assert');

const evalSrc = src => (0, eval)(src);
assert.throws(() => evalSrc('"a\nb"'), SyntaxError);
assert.throws(() => evalSrc("'a\rb'"), SyntaxError);
assert.throws(() => evalSrc('"a\r\nb"'), SyntaxError);
assert.throws(() => evalSrc('"a\\x41\nb"'), SyntaxError);
assert.strictEqual(evalSrc('"a b"'), 'a b');
assert.strictEqual(evalSrc('"a b"'), 'a b');
assert.strictEqual(evalSrc('"a\\\nb"'), 'ab');
assert.strictEqual(evalSrc('"a\\\r\nb"'), 'ab');
assert.strictEqual(evalSrc('`a\nb`'), 'a\nb');

try {
  evalSrc('const x = 1;\nconst s = "abc\ndef";');
  assert.fail('expected SyntaxError');
} catch (e) {
  assert.ok(e instanceof SyntaxError);
  assert.ok(!e.message.includes('def'), e.message);
}

console.log('string raw newline OK');
