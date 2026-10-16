const assert = require('node:assert');

const evalSrc = src => (0, eval)(src);
assert.strictEqual(evalSrc('"a\\\nb"'), 'ab');
assert.strictEqual(evalSrc('"a\\\r\nb"'), 'ab');
assert.strictEqual(evalSrc('"a\\\rb"'), 'ab');
assert.strictEqual(evalSrc("'a\\ b'"), 'ab');
assert.strictEqual(evalSrc("'a\\ b'"), 'ab');
assert.strictEqual(evalSrc('"a\\\n\\\nb"'), 'ab');
assert.strictEqual(evalSrc('`a\\\nb`'), 'ab');
assert.strictEqual(evalSrc('`a\\\r\nb`'), 'ab');
assert.strictEqual(evalSrc('`a\\ b`'), 'ab');

assert.deepStrictEqual(evalSrc('(s => [s[0], s.raw[0]])`x\\\ny`'), ['xy', 'x\\\ny']);
assert.deepStrictEqual(evalSrc('(s => [s[0], s.raw[0]])`x\\\r\ny`'), ['xy', 'x\\\ny']);
assert.deepStrictEqual(evalSrc('(s => s.raw[0])`a\r\nb`'), 'a\nb');
assert.deepStrictEqual(evalSrc('(s => s.raw[0])`a\rb`'), 'a\nb');
assert.strictEqual(evalSrc('`a\r\nb`'), 'a\nb');

console.log('line continuation OK');
