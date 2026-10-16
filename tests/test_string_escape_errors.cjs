const assert = require('node:assert');

const evalSrc = src => (0, eval)(src);
const syntax = (src, message) => assert.throws(() => evalSrc(src), e => e instanceof SyntaxError && (!message || e.message === message), src);

syntax('"\\u12xy"', 'Invalid Unicode escape sequence');
syntax('"\\xZZ"', 'Invalid hexadecimal escape sequence');
syntax('"\\u{110000}"', 'Undefined Unicode code-point');
syntax('"\\u{}"', 'Invalid Unicode escape sequence');
syntax("'\\x4g'", 'Invalid hexadecimal escape sequence');
syntax('"\\u{41"', 'Invalid Unicode escape sequence');
syntax('"\\u{"', 'Invalid Unicode escape sequence');
syntax('"\\x4"', 'Invalid hexadecimal escape sequence');

syntax('"use strict"; "\\8"', '\\8 and \\9 are not allowed in strict mode.');
syntax('"use strict"; "\\9"', '\\8 and \\9 are not allowed in strict mode.');
syntax('"use strict"; "\\08"', 'Octal escape sequences are not allowed in strict mode.');
syntax('"use strict"; "\\1"', 'Octal escape sequences are not allowed in strict mode.');
syntax('function f() { "use strict"; return "\\9"; }', '\\8 and \\9 are not allowed in strict mode.');

syntax('`\\u{41`', 'Invalid Unicode escape sequence');
syntax('`\\x4`', 'Invalid hexadecimal escape sequence');
syntax('`\\u{110000}`', 'Undefined Unicode code-point');
syntax('`\\8`', '\\8 and \\9 are not allowed in template strings.');
syntax('`\\1`', 'Octal escape sequences are not allowed in template strings.');
syntax('`\\08`', 'Octal escape sequences are not allowed in template strings.');
syntax('`a${`\\9`}`', '\\8 and \\9 are not allowed in template strings.');
syntax('String.raw`a${`\\9`}`', '\\8 and \\9 are not allowed in template strings.');
syntax('(function () { return `\\1`; })', 'Octal escape sequences are not allowed in template strings.');

assert.strictEqual(evalSrc('`\\0`'), '\0');
assert.strictEqual(evalSrc('String.raw`${String.raw`\\9`}\\8`'), '\\9\\8');

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
