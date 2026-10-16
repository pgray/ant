const assert = require('node:assert');

const evalSrc = src => (0, eval)(src);
const syntax = (src, message) => assert.throws(() => evalSrc(src), e => e instanceof SyntaxError && (!message || e.message === message), src);

syntax('"\\u12xy"', 'Invalid Unicode escape sequence');
syntax('"\\xZZ"', 'Invalid hexadecimal escape sequence');
syntax('"\\u{110000}"', 'Undefined Unicode code-point');
syntax('"\\u{}"', 'Invalid Unicode escape sequence');
syntax("'\\x4g'", 'Invalid hexadecimal escape sequence');
syntax('"\\u{41"');

assert.strictEqual(evalSrc('"\\u{41}\\x41\\u0041\\u{0000000041}"'), 'AAAA');
assert.strictEqual(evalSrc('"\\8\\9"'), '89');
assert.strictEqual(evalSrc('"\\01"'), '\u0001');
assert.strictEqual(evalSrc('"\\uD83D\\uDE00"'), '\u{1F600}');

const cooked = evalSrc('(s => s[0])');
assert.strictEqual(evalSrc('(s => s[0])`\\u{FFFFFFFFF}`'), undefined);
assert.strictEqual(evalSrc('(s => s[0])`\\8`'), undefined);
assert.strictEqual(evalSrc('(s => s[0])`\\08`'), undefined);
assert.strictEqual(evalSrc('(s => s[0])`\\xZ`'), undefined);
assert.strictEqual(typeof cooked, 'function');

console.log('string escape errors OK');
