const assert = require('node:assert');

const view = new DataView(new ArrayBuffer(16), 3, 10);
view.setInt16(1, -2, true);
assert.strictEqual(view.getInt16(1, true), -2);
assert.strictEqual(view.getUint16(1, true), 65534);
view.setUint32(2, 2 ** 32 + 5);
assert.strictEqual(view.getUint32(2), 5);
view.setFloat16(0, 65504);
assert.strictEqual(view.getFloat16(0), 65504);
view.setBigInt64(1, 2n ** 64n + 3n, true);
assert.strictEqual(view.getBigInt64(1, true), 3n);
view.setFloat64(2, -0);
assert.ok(Object.is(view.getFloat64(2), -0));

for (const kind of ['Int8', 'Uint8', 'Int16', 'Uint16', 'Int32', 'Uint32', 'Float16', 'Float32', 'Float64', 'BigInt64', 'BigUint64']) {
  assert.strictEqual(DataView.prototype['get' + kind].length, 1, kind);
  assert.strictEqual(DataView.prototype['set' + kind].length, 2, kind);
}

const bytes = new DataView(new ArrayBuffer(8));
assert.strictEqual(bytes.getUint8(), 0);
assert.strictEqual(bytes.getUint8('2'), 0);
assert.strictEqual(bytes.getUint8(1.9), 0);
assert.throws(() => bytes.getUint8(-1), RangeError);
assert.throws(() => bytes.getUint32(6), RangeError);
assert.throws(() => bytes.setUint8(8, 1), RangeError);
assert.throws(() => bytes.setBigInt64(0, 1), TypeError);
assert.throws(() => DataView.prototype.getUint8.call({}, 0), TypeError);

const order = [];
assert.throws(() => bytes.setUint8({ valueOf() { order.push('offset'); return 9; } }, { valueOf() { order.push('value'); return 1; } }), RangeError);
assert.deepStrictEqual(order, ['offset', 'value']);

const shrinking = new ArrayBuffer(4);
assert.throws(() => new DataView(shrinking).setUint8(0, { valueOf() { shrinking.transfer(); return 1; } }), TypeError);

const buffer = new ArrayBuffer(8);
const detached = new DataView(buffer, 2, 4);
const byteLength = v => v.byteLength;
const byteOffset = v => v.byteOffset;
for (let i = 0; i < 5000; i++) { byteLength(detached); byteOffset(detached); }
assert.deepStrictEqual([byteLength(detached), byteOffset(detached)], [4, 2]);

buffer.transfer();
assert.throws(() => byteLength(detached), TypeError);
assert.throws(() => byteOffset(detached), TypeError);
assert.throws(() => detached.getUint8(0), TypeError);
assert.throws(() => detached.setFloat64(0, 1), TypeError);
assert.strictEqual(detached.buffer, buffer);

console.log('dataview accessors OK');
