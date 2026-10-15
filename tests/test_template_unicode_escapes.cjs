const assert = require('node:assert');

assert.strictEqual(`a\u{41}b`, 'aAb');
assert.strictEqual(`\u{1F600}`, '\u{1F600}');
assert.strictEqual(`\u{0}x`.length, 2);
assert.strictEqual(`x${1}\u{42}${2}C`, 'x1B2C');
assert.strictEqual(`\u{10FFFF}`.codePointAt(0), 0x10FFFF);

const tag = strings => [strings[0], strings.raw[0]];
assert.deepStrictEqual(tag`a\u{41}b`, ['aAb', 'a\\u{41}b']);
assert.deepStrictEqual(tag`\u{110000}`, [undefined, '\\u{110000}']);
assert.deepStrictEqual(tag`\u{}`, [undefined, '\\u{}']);
assert.deepStrictEqual(tag`\u{41`, [undefined, '\\u{41']);

for (let i = 0; i < 3; i++) assert.strictEqual(`<\u{3B1}>`, '<α>');

console.log('template unicode escapes OK');
