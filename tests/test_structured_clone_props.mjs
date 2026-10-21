import assert from 'node:assert/strict';

const o = {};
Object.defineProperty(o, 'hidden', { value: 1, enumerable: false });
Object.defineProperty(o, 'acc', { get() { return 42; }, enumerable: true });
o.plain = 2;

const c = structuredClone(o);
assert.deepEqual(Object.getOwnPropertyNames(c), ['acc', 'plain']);
assert.deepEqual(Object.getOwnPropertyDescriptor(c, 'acc'), { value: 42, writable: true, enumerable: true, configurable: true });

const sab = new SharedArrayBuffer(4);
const copy = structuredClone(sab);
assert.ok(copy instanceof SharedArrayBuffer);
new Int32Array(copy)[0] = 5;
assert.equal(new Int32Array(sab)[0], 5);

const err = structuredClone(new TypeError('x'));
assert.deepEqual(Object.keys(err), []);
assert.equal(err.message, 'x');

console.log('structured clone props: ok');
