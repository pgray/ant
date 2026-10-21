const assert = require('node:assert/strict');

const ROUNDS = 400;

function readAll(a, n) {
  const out = [];
  for (let i = 0; i < n; i++) out.push(a[i]);
  return out;
}

function sum(a) {
  let s = 0;
  for (let i = 0; i < a.length; i++) s += a[i];
  return s;
}

function writeAll(a, values) {
  for (let i = 0; i < values.length; i++) a[i] = values[i];
  return a;
}

function readAt(a, i) { return a[i]; }
function writeAt(a, i, v) { a[i] = v; return a; }

function hot(fn) {
  let last;
  for (let r = 0; r < ROUNDS; r++) last = fn();
  return last;
}

const values = [0, 1, -1, 1.5, -1.5, 2.5, 127, 128, 255, 256, 300, -129, 65535, 65536, 2 ** 31, -(2 ** 31) - 1, 2 ** 32 + 7, 2 ** 53, 1e20, -0, NaN, Infinity, -Infinity];
const ctors = [Int8Array, Uint8Array, Uint8ClampedArray, Int16Array, Uint16Array, Int32Array, Uint32Array, Float32Array, Float64Array];

for (const C of ctors) {
  const expected = Array.from(new C(values));
  const got = hot(() => Array.from(writeAll(new C(values.length), values)));
  assert.deepEqual(got, expected, `${C.name} writes`);
  const src = new C(values);
  assert.deepEqual(hot(() => readAll(src, values.length)), expected, `${C.name} reads`);
  assert.deepEqual(hot(() => readAll(src, values.length + 3)).slice(-3), [undefined, undefined, undefined], `${C.name} past end`);
}

const u8 = new Uint8Array(64).map((_, i) => i * 3);
assert.equal(hot(() => sum(u8)), 6048);
assert.equal(hot(() => readAt(u8, -1)), undefined);
assert.equal(hot(() => readAt(u8, 64)), undefined);
assert.equal(hot(() => readAt(u8, 1.5)), undefined);
assert.equal(hot(() => readAt(u8, -0)), 0);
assert.equal(hot(() => readAt(u8, '5')), 15);

Uint8Array.prototype[100] = 'proto';
Object.prototype[101] = 'object';
assert.equal(hot(() => readAt(u8, 100)), undefined);
assert.equal(hot(() => readAt(u8, 101)), undefined);
delete Uint8Array.prototype[100];
delete Object.prototype[101];

const oob = new Uint8Array(4);
hot(() => writeAt(oob, 10, 1));
hot(() => writeAt(oob, -1, 1));
assert.deepEqual(Object.keys(oob), ['0', '1', '2', '3']);
assert.equal(oob[10], undefined);

const base = new Uint8Array(32).map((_, i) => i);
const sub = base.subarray(8, 24);
assert.equal(hot(() => sum(sub)), 248);
assert.equal(hot(() => readAt(sub, 16)), undefined);
hot(() => writeAt(sub, 0, 200));
assert.equal(base[8], 200);

const buf = Buffer.from([1, 2, 3, 250]);
assert.equal(hot(() => sum(buf)), 256);
hot(() => writeAt(buf, 1, 513));
assert.equal(buf[1], 1);

const f64 = new Float64Array(new Uint32Array([1, 0x7ff80000, 0, 0x7ff00000]).buffer);
assert.ok(Number.isNaN(hot(() => readAt(f64, 0))));
assert.equal(hot(() => readAt(f64, 1)), Infinity);
const f32 = new Float32Array(new Uint32Array([0x7fc00001]).buffer);
assert.ok(Number.isNaN(hot(() => readAt(f32, 0))));
assert.equal(hot(() => readAt(new Float32Array([0.1]), 0)), Math.fround(0.1));

const detachable = new Uint8Array(new ArrayBuffer(16)).fill(7);
assert.equal(hot(() => sum(detachable)), 112);
const moved = detachable.buffer.transfer();
assert.equal(detachable.length, 0);
assert.equal(hot(() => sum(detachable)), 0);
assert.equal(hot(() => readAt(detachable, 0)), undefined);
hot(() => writeAt(detachable, 0, 1));
assert.equal(detachable[0], undefined);
assert.equal(new Uint8Array(moved)[0], 7);

function poly(a, i) { return a[i]; }
const mixed = [new Uint8Array([9]), [8], { 0: 7 }, new Float64Array([6.5]), 'x', new Int16Array([-5])];
for (let r = 0; r < ROUNDS; r++)
  for (const m of mixed) poly(m, 0);
assert.deepEqual(mixed.map(m => poly(m, 0)), [9, 8, 7, 6.5, 'x', -5]);

function polyWrite(a, v) { a[0] = v; return a[0]; }
const targets = [new Uint8Array(1), [0], new Float32Array(1), new Uint8ClampedArray(1)];
for (let r = 0; r < ROUNDS; r++)
  for (const t of targets) polyWrite(t, r & 1 ? 1.5 : 300);
assert.deepEqual(targets.map(t => polyWrite(t, 300)), [44, 300, 300, 255]);
assert.deepEqual(targets.map(t => polyWrite(t, 1.5)), [1, 1.5, 1.5, 2]);

const coerced = new Int32Array(2);
hot(() => writeAt(coerced, 0, '42'));
hot(() => writeAt(coerced, 1, { valueOf: () => 9 }));
assert.deepEqual(Array.from(coerced), [42, 9]);

function at(a, i) { return a[i]; }
function viaInline(a, n) {
  let s = 0;
  for (let i = 0; i < n; i++) { const v = at(a, i); s += v === undefined ? 1000 : v; }
  return s;
}
const inl = new Int16Array([-3, 4, 5]);
assert.equal(hot(() => viaInline(inl, 3)), 6);
assert.equal(hot(() => viaInline(inl, 5)), 2006);
assert.equal(hot(() => viaInline([1, 2, 3], 3)), 6);
const inlDetach = new Float64Array(new ArrayBuffer(16)).fill(0.25);
assert.equal(hot(() => viaInline(inlDetach, 2)), 0.5);
inlDetach.buffer.transfer();
assert.equal(hot(() => viaInline(inlDetach, 2)), 2000);

console.log('ok');
