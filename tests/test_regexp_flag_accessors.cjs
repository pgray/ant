const assert = require('node:assert');
const util = require('node:util');

const flagNames = ['hasIndices', 'global', 'ignoreCase', 'multiline', 'dotAll', 'unicode', 'unicodeSets', 'sticky'];

assert.deepStrictEqual(Object.getOwnPropertyNames(/a/dgimsuy), ['lastIndex']);
assert.deepStrictEqual(Object.getOwnPropertyNames(new RegExp('a', 'v')), ['lastIndex']);
const lastIndex = Object.getOwnPropertyDescriptor(/a/, 'lastIndex');
assert.deepStrictEqual([lastIndex.writable, lastIndex.enumerable, lastIndex.configurable], [true, false, false]);

for (const name of ['source', 'flags', ...flagNames]) {
  const desc = Object.getOwnPropertyDescriptor(RegExp.prototype, name);
  assert.strictEqual(typeof desc.get, 'function', name);
  assert.strictEqual(desc.get.name, `get ${name}`);
  assert.strictEqual(desc.set, undefined, name);
  assert.strictEqual(desc.enumerable, false, name);
  assert.strictEqual(desc.configurable, true, name);
}

for (const name of flagNames) {
  assert.strictEqual(RegExp.prototype[name], undefined, name);
  assert.throws(() => Object.create(/a/)[name], TypeError, name);
}
assert.strictEqual(RegExp.prototype.source, '(?:)');
assert.strictEqual(RegExp.prototype.flags, '');
assert.throws(() => Object.create(/a/).source, TypeError);

const re = /x/g;
re.global = 5;
assert.strictEqual(re.global, true);

class Over extends RegExp {
  get global() { return 'over'; }
}
assert.strictEqual(new Over('a', 'g').global, 'over');
assert.strictEqual(new Over('a').flags, 'g');

assert.strictEqual(/x/dgimsy.flags, 'dgimsy');
assert.strictEqual(new RegExp('x', 'yg').flags, 'gy');
assert.strictEqual(new RegExp('x', 'v').unicodeSets, true);
assert.strictEqual(new RegExp('a/b').source, 'a\\/b');
assert.strictEqual(new RegExp('\n').source, '\\n');
assert.strictEqual(new RegExp('').source, '(?:)');
assert.strictEqual(String(new RegExp('a/b', 'gi')), '/a\\/b/gi');
assert.strictEqual(util.inspect(new RegExp('a/b', 'g')), '/a\\/b/g');

const compiled = /a/g;
compiled.lastIndex = 3;
compiled.compile('b', 'i');
assert.deepStrictEqual([compiled.source, compiled.flags, compiled.global, compiled.lastIndex], ['b', 'i', false, 0]);

const warm = /a/g;
for (let i = 0; i < 100; i++) { warm.flags; 'aa'.replace(warm, 'b'); 'aa'.replace(/a/g, 'c'); }
const globalDesc = Object.getOwnPropertyDescriptor(RegExp.prototype, 'global');
Object.defineProperty(RegExp.prototype, 'global', { get() { return false; }, configurable: true });
try {
  assert.strictEqual(warm.flags, '');
  assert.strictEqual('aa'.replace(warm, 'b'), 'ba');
  assert.strictEqual('aa'.replace(/a/g, 'c'), 'ca');
} finally {
  Object.defineProperty(RegExp.prototype, 'global', globalDesc);
}
assert.strictEqual(warm.flags, 'g');
assert.strictEqual('aa'.replace(/a/g, 'c'), 'cc');

const shadowed = /a/g;
Object.defineProperty(shadowed, 'global', { value: false });
assert.strictEqual(shadowed.flags, '');
assert.strictEqual('aa'.replace(shadowed, 'b'), 'ba');

const cloned = structuredClone(/a\/b/gi);
assert.ok(cloned instanceof RegExp);
assert.strictEqual(String(cloned), '/a\\/b/gi');

assert.strictEqual(util.types.isRegExp(/a/), true);
assert.strictEqual(util.types.isRegExp(Object.create(/a/)), false);

console.log('regexp flag accessors OK');
