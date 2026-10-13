const assert = require('node:assert');

function get(x) { return x.foo; }
function call(s, i) { return s.charCodeAt(i); }
function uncurried(f, s, i) { return f.call(s, i); }
function fixed(n) { return n.toFixed(1); }
function text(b) { return b.toString(); }

for (let i = 0; i < 3000; i++) {
  call('abc', i % 3);
  uncurried(String.prototype.charCodeAt, 'abc', i % 3);
  fixed(i);
  text(i % 2 === 0);
  get('x');
  get(1);
}

String.prototype.foo = 'S';
Number.prototype.foo = 'N';
assert.deepStrictEqual([get('x'), get(1), get(true)], ['S', 'N', undefined]);

for (let i = 0; i < 3000; i++) get('y');
delete String.prototype.foo;
Object.prototype.foo = 'O';
assert.deepStrictEqual([get('x'), get(2)], ['O', 'N']);
delete Object.prototype.foo;
delete Number.prototype.foo;

const original = String.prototype.charCodeAt;
String.prototype.charCodeAt = function (i) { return -i; };
assert.strictEqual(call('abc', 2), -2);
for (let i = 0; i < 3000; i++) call('abc', 1);
assert.strictEqual(call('abc', 2), -2);
String.prototype.charCodeAt = original;

assert.deepStrictEqual(
  [call('abc', 2), call('é', 0), call('abc', 9), call('abc', -1), call('abc', 1.7), call(new String('abc'), 0), call('abc', '1'), call('abc')],
  [99, 233, NaN, NaN, 98, 97, 98, 97]
);
assert.deepStrictEqual(
  [uncurried(original, 'abc', 0), uncurried(original, 'é', 0), uncurried(original, 'abc', 5)],
  [97, 233, NaN]
);

Object.defineProperty(Number.prototype, 'toFixed', { get() { return () => 'getter'; }, configurable: true });
assert.strictEqual(fixed(3), 'getter');

let sum = 0;
for (let i = 0; i < 100000; i++) sum += 'hello world'.length + i.toString().length + 'xyz'.charCodeAt(i % 3);
assert.strictEqual(sum, 13688889);

console.log('jit primitive field ic OK');
