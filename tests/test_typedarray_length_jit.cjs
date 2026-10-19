const assert = require('node:assert/strict');

const ROUNDS = 400;
const TA = Object.getPrototypeOf(Uint8Array.prototype);
const intrinsic = Object.getOwnPropertyDescriptor(TA, 'length');

function len(a) { return a.length; }
function loopLen(a) { let n = 0; for (let i = 0; i < a.length; i++) n++; return n; }
function hot(fn) { let last; for (let r = 0; r < ROUNDS; r++) last = fn(); return last; }

const u8 = new Uint8Array(16);
const f64 = new Float64Array(5);
assert.equal(hot(() => len(u8)), 16);
assert.equal(hot(() => loopLen(u8)), 16);
assert.equal(hot(() => len(f64)), 5);
assert.equal(hot(() => len(u8.subarray(4, 9))), 5);
assert.equal(hot(() => len(Buffer.alloc(7))), 7);

const own = new Uint8Array(8);
hot(() => len(own));
Object.defineProperty(own, 'length', { value: 99 });
assert.equal(hot(() => len(own)), 99);
assert.equal(hot(() => len(new Uint8Array(3))), 3);

const shadowed = new Uint8Array(4);
hot(() => len(shadowed));
Object.defineProperty(Uint8Array.prototype, 'length', { value: 'proto', configurable: true });
assert.equal(hot(() => len(shadowed)), 'proto');
assert.equal(hot(() => len(new Int16Array(2))), 2);
delete Uint8Array.prototype.length;
assert.equal(hot(() => len(shadowed)), 4);

const redefined = new Uint16Array(6);
hot(() => len(redefined));
Object.defineProperty(TA, 'length', { get() { return 'getter'; }, configurable: true });
assert.equal(hot(() => len(redefined)), 'getter');
Object.defineProperty(TA, 'length', intrinsic);
assert.equal(hot(() => len(redefined)), 6);

const reproto = new Uint8Array(5);
hot(() => len(reproto));
Object.setPrototypeOf(reproto, { length: 'plain' });
assert.equal(hot(() => len(reproto)), 'plain');

const midSwap = new Uint8Array(9);
hot(() => len(midSwap));
const original = Object.getPrototypeOf(Uint8Array.prototype);
Object.setPrototypeOf(Uint8Array.prototype, { get length() { return 'swapped'; } });
assert.equal(hot(() => len(midSwap)), 'swapped');
Object.setPrototypeOf(Uint8Array.prototype, original);
assert.equal(hot(() => len(midSwap)), 9);

class Sub extends Uint8Array {}
const sub = new Sub(11);
assert.equal(hot(() => len(sub)), 11);
class SubLen extends Uint8Array { get length() { return 'sub'; } }
assert.equal(hot(() => len(new SubLen(3))), 'sub');

const detachable = new Uint8Array(new ArrayBuffer(12));
assert.equal(hot(() => len(detachable)), 12);
detachable.buffer.transfer();
assert.equal(hot(() => len(detachable)), 0);
assert.equal(hot(() => loopLen(detachable)), 0);

const mixed = [new Uint8Array(1), [1, 2], 'abc', new Float32Array(4), { length: 'obj' }, new DataView(new ArrayBuffer(2))];
for (let r = 0; r < ROUNDS; r++) for (const m of mixed) len(m);
assert.deepEqual(mixed.map(len), [1, 2, 3, 4, 'obj', undefined]);

function elems(a) { let s = 0; for (let i = 0; i < a.length; i++) s += a[i]; return s; }
const kinds = [Int8Array, Uint8Array, Uint8ClampedArray, Int16Array, Uint16Array, Int32Array, Uint32Array, Float32Array, Float64Array];
for (const C of kinds) {
  const a = C.from({ length: 10 }, (_, i) => i - 3);
  const expected = Array.from(a).reduce((x, y) => x + y, 0);
  assert.equal(hot(() => elems(a)), expected, C.name);
}
const switching = [new Uint32Array([7, 8]), new Int16Array([-1, 2]), new Float64Array([0.5, 0.25])];
for (let r = 0; r < ROUNDS; r++) elems(switching[0]);
assert.deepEqual(switching.map(elems), [15, 1, 0.75]);

function fill(a, v) { for (let i = 0; i < a.length; i++) a[i] = v; return a; }
const u32 = new Uint32Array(4);
hot(() => fill(u32, 3));
assert.deepEqual(Array.from(fill(u32, -1)), [4294967295, 4294967295, 4294967295, 4294967295]);
assert.deepEqual(Array.from(fill(new Uint8ClampedArray(2), 300)), [255, 255]);
assert.deepEqual(Array.from(fill(new Float32Array(2), 1.5)), [1.5, 1.5]);

console.log('ok');
