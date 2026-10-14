const assert = require('node:assert');

const sym = Symbol('s');
const target = { get plain() { return this === proxy; }, get [sym]() { return this === proxy; }, value: 1, [Symbol.for('v')]: 2 };
const proxy = new Proxy(target, {});
assert.deepStrictEqual([proxy.plain, proxy[sym], proxy.value, proxy[Symbol.for('v')], proxy.missing], [true, true, 1, 2, undefined]);
assert.throws(() => new Proxy(new Uint8Array(3), {}).length, TypeError);
assert.throws(() => new Proxy(new Uint8Array(3), {}).byteLength, TypeError);

const detached = new ArrayBuffer(8);
structuredClone(detached, { transfer: [detached] });
assert.throws(() => Buffer.from(detached), TypeError);

const B = Buffer.prototype;
assert.throws(() => B.readUInt8.call({}, 0), { name: 'RangeError', code: 'ERR_OUT_OF_RANGE', message: 'The value of "offset" is out of range. It must be >= 0 and <= NaN. Received 0' });
assert.throws(() => B.writeUInt8.call({}, 1, 0), { name: 'RangeError', code: 'ERR_OUT_OF_RANGE' });
assert.throws(() => B.readUInt8.call({}, 'x'), { name: 'TypeError', code: 'ERR_INVALID_ARG_TYPE' });
assert.throws(() => Buffer.alloc(2).readUInt8(5), { name: 'RangeError', code: 'ERR_OUT_OF_RANGE' });
assert.strictEqual(Buffer.from([1, 2, 3]).readUInt8(2), 3);

console.log('receiver edges OK');
