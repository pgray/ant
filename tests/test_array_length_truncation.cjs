const assert = require('node:assert');

function pinned() {
  const a = [0, 1, 2, 3, 4];
  Object.defineProperty(a, 2, { value: 2, configurable: false });
  return a;
}

assert.throws(() => { 'use strict'; pinned().length = 0; }, TypeError);

{
  const a = pinned();
  try { (function () { 'use strict'; a.length = 0; })(); } catch {}
  assert.strictEqual(a.length, 3);
  assert.deepStrictEqual(Object.keys(a), ['0', '1', '2']);
}

{
  const a = pinned();
  new Function('a', 'a.length = 0;')(a);
  assert.strictEqual(a.length, 3);
  assert.deepStrictEqual(Object.keys(a), ['0', '1', '2']);
}

{
  const a = pinned();
  assert.throws(() => Object.defineProperty(a, 'length', { value: 0 }), TypeError);
  assert.strictEqual(a.length, 3);
}

assert.strictEqual(Reflect.defineProperty(pinned(), 'length', { value: 0 }), false);

{
  const a = [0, 1, 2, 3];
  Object.defineProperty(a, 3, { value: 3, configurable: false });
  a.length = 3;
  a.length = 4;
  assert.strictEqual(a.length, 4);
  assert.deepStrictEqual(Object.keys(a), ['0', '1', '2', '3']);
}

{
  const a = [0];
  let reads = 0;
  Object.defineProperty(a, 1, { get() { reads++; return 1; }, configurable: true, enumerable: true });
  a.length = 1;
  assert.strictEqual(a.length, 1);
  assert.deepStrictEqual(Object.keys(a), ['0']);
  assert.strictEqual(1 in a, false);
  assert.strictEqual(reads, 0);
}

{
  const a = [];
  a[1000000] = 1;
  a.length = 0;
  assert.strictEqual(a.length, 0);
  assert.deepStrictEqual(Object.getOwnPropertyNames(a), ['length']);
}

{
  const a = [0, 1];
  a[5000] = 'x';
  Object.defineProperty(a, 7, { get() { return 7; }, configurable: true, enumerable: true });
  a.length = 1;
  assert.deepStrictEqual(Object.getOwnPropertyNames(a), ['0', 'length']);
  a.length = 10;
  assert.strictEqual(a[7], undefined);
  assert.strictEqual(a[5000], undefined);
}

{
  const a = [0, 1, 2, 3, 4];
  Object.defineProperty(a, 1, { value: 1, configurable: false });
  Object.defineProperty(a, 3, { value: 3, configurable: false });
  a.length = 0;
  assert.strictEqual(a.length, 4);
  assert.deepStrictEqual(Object.keys(a), ['0', '1', '2', '3']);
}

{
  const a = [1];
  a.length = 3;
  assert.strictEqual(a.length, 3);
  assert.strictEqual(1 in a, false);
}

{
  const frozen = Object.freeze([1, 2]);
  assert.throws(() => { 'use strict'; frozen.length = 0; }, TypeError);
  assert.strictEqual(frozen.length, 2);
  const sealed = Object.seal([1, 2]);
  assert.throws(() => { 'use strict'; sealed.length = 0; }, TypeError);
  assert.strictEqual(sealed.length, 2);
  assert.deepStrictEqual(Object.keys(sealed), ['0', '1']);
}

console.log('array length truncation ok');
