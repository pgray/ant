const assert = require('node:assert');

function fib(n) { return n < 2 ? n : fib(n - 1) + fib(n - 2); }
for (let i = 0; i < 300; i++) assert.strictEqual(fib(15), 610);
const coldFib = new Function('n', 'const f = m => m < 2 ? m : f(m - 1) + f(m - 2); return f(n);');
assert.strictEqual(fib(15.5), coldFib(15.5));
assert.strictEqual(fib('3'), coldFib('3'));
assert.strictEqual(fib('12'), coldFib('12'));

function sum(node) { return node === null ? 0 : node.v + sum(node.next); }
let list = null;
for (let i = 0; i < 50; i++) list = { v: i, next: list };
for (let i = 0; i < 2000; i++) assert.strictEqual(sum(list), 1225);
list.next.next.v = 'x';
const coldSum = new Function('node', 'let s = []; for (; node !== null; node = node.next) s.push(node.v); let r = 0; for (let i = s.length - 1; i >= 0; i--) r = s[i] + r; return r;');
assert.strictEqual(sum(list), coldSum(list));

function depth(n, limit) {
  if (n === limit) throw new RangeError('at ' + n);
  return depth(n + 1, limit) + 1;
}
for (let i = 0; i < 2000; i++) {
  try {
    depth(0, 4);
    assert.fail('no throw');
  } catch (e) {
    assert.strictEqual(e.message, 'at 4');
  }
}

function unbounded(n) { return unbounded(n + 1) + 1; }
for (let i = 0; i < 3; i++) assert.throws(() => unbounded(0), RangeError);

function isEven(n) { return n === 0 ? true : isOdd(n - 1); }
function isOdd(n) { return n === 0 ? false : isEven(n - 1); }
for (let i = 0; i < 2000; i++) {
  assert.strictEqual(isEven(10), true);
  assert.strictEqual(isOdd(7), true);
}

function makeCounter() {
  let calls = 0;
  function walk(n) { calls++; return n <= 0 ? 0 : walk(n - 1) + walk(n - 2); }
  return () => { calls = 0; walk(10); return calls; };
}
const counter = makeCounter();
for (let i = 0; i < 1000; i++) assert.strictEqual(counter(), 287);

function Tree(l, r) { this.left = l; this.right = r; }
function build(d) { return d > 0 ? new Tree(build(d - 1), build(d - 1)) : new Tree(null, null); }
function count(t) { return t.left === null ? 1 : 1 + count(t.left) + count(t.right); }
for (let i = 0; i < 200; i++) assert.strictEqual(count(build(8)), 511);
Tree.prototype.extra = 1;
assert.strictEqual(count(build(6)), 127);

console.log('ok');
