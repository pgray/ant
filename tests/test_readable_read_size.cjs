const assert = require('node:assert');
const { Readable } = require('node:stream');

const r = new Readable({ read() {} });
r.push(Buffer.from('12345'));
r.push(Buffer.from('678'));
assert.strictEqual(r.readableLength, 8);
assert.strictEqual(String(r.read(4)), '1234');
assert.strictEqual(String(r.read(2)), '56');
assert.strictEqual(r.read(5), null);
assert.strictEqual(String(r.read()), '78');
r.unshift(Buffer.from('zz'));
assert.strictEqual(String(r.read()), 'zz');

const joined = new Readable({ read() {} });
for (const part of ['ab', 'cd', 'ef']) joined.push(Buffer.from(part));
assert.strictEqual(String(joined.read(5)), 'abcde');
assert.strictEqual(String(joined.read(1)), 'f');

const ended = new Readable({ read() {} });
ended.push(Buffer.from('xyz'));
ended.push(null);
assert.strictEqual(String(ended.read(10)), 'xyz');

const big = new Readable({ highWaterMark: 4, read() {} });
assert.strictEqual(big.read(100), null);
assert.ok(big.readableHighWaterMark >= 100);

const objects = new Readable({ objectMode: true, read() {} });
objects.push({ a: 1 });
objects.push({ b: 2 });
assert.deepStrictEqual(objects.read(5), { a: 1 });
assert.strictEqual(objects.readableObjectMode, true);

console.log('readable read size: ok');
