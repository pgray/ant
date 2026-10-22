const assert = require('node:assert');

const iterate = String.prototype[Symbol.iterator];
const StringIteratorPrototype = Object.getPrototypeOf(''[Symbol.iterator]());
const next = StringIteratorPrototype.next;

assert.deepStrictEqual([...iterate.call(42)], ['4', '2']);
assert.deepStrictEqual([...iterate.call(true)], ['t', 'r', 'u', 'e']);
assert.deepStrictEqual([...iterate.call(5n)], ['5']);
assert.deepStrictEqual([...iterate.call(new String('xy'))], ['x', 'y']);
assert.deepStrictEqual([...iterate.call({ toString() { return 'ok'; } })], ['o', 'k']);

for (const receiver of [null, undefined, Symbol('s'), { [Symbol.toPrimitive]() { return Symbol('p'); } }]) {
  assert.throws(() => iterate.call(receiver), TypeError);
}

const original = new Error('mine');
assert.throws(() => iterate.call({ toString() { throw original; } }), e => e === original);

{
  const hints = [];
  const it = iterate.call({ [Symbol.toPrimitive](hint) { hints.push(hint); return 'ab'; } });
  it.next();
  it.next();
  it.next();
  assert.deepStrictEqual(hints, ['string']);
}

{
  const source = { s: 'ab', toString() { return this.s; } };
  const it = iterate.call(source);
  source.s = 'zz';
  assert.deepStrictEqual([...it], ['a', 'b']);
}

const badReceivers = {
  'plain object': {},
  'prototype': StringIteratorPrototype,
  'array iterator': [][Symbol.iterator](),
  'proxy': new Proxy(''[Symbol.iterator](), {}),
  'null': null,
  'undefined': undefined,
  'Object.create': Object.create(StringIteratorPrototype),
  'string': 'abc',
};
for (const [label, receiver] of Object.entries(badReceivers)) {
  assert.throws(() => next.call(receiver), TypeError, label);
}

{
  const it = 'ab'[Symbol.iterator]();
  Object.setPrototypeOf(it, null);
  assert.deepStrictEqual(next.call(it), { value: 'a', done: false });
  assert.deepStrictEqual(next.call(it), { value: 'b', done: false });
  assert.deepStrictEqual(next.call(it), { value: undefined, done: true });
}

assert.deepStrictEqual([...'A😀B'], ['A', '😀', 'B']);
assert.deepStrictEqual([...'a\ud800b'], ['a', '\ud800', 'b']);

{
  const it = 'a'[Symbol.iterator]();
  it.next();
  assert.deepStrictEqual(it.next(), { value: undefined, done: true });
  assert.deepStrictEqual(it.next(), { value: undefined, done: true });
}

console.log('string iterator receiver ok');
