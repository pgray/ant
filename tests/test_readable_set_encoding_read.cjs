const assert = require('node:assert');
const { Readable } = require('node:stream');

const euro = Buffer.from('€');

const split = new Readable({ read() {} });
split.setEncoding('utf8');
split.push(Buffer.concat([Buffer.from('ab'), euro.subarray(0, 1)]));
assert.strictEqual(split.readableLength, 2);
split.push(Buffer.concat([euro.subarray(1), Buffer.from('cdé')]));
assert.strictEqual(split.readableLength, 6);
assert.strictEqual(split.read(2), 'ab');
assert.strictEqual(split.read(2), '€c');
assert.strictEqual(split.readableLength, 2);
assert.strictEqual(split.read(10), null);
assert.strictEqual(split.read(), 'dé');
assert.strictEqual(split.readableLength, 0);

const late = new Readable({ read() {} });
late.push(Buffer.from('héllo'));
late.push(Buffer.from(' wörld'));
assert.strictEqual(late.readableLength, 13);
late.read(1);
late.setEncoding('utf8');
assert.strictEqual(late.readableLength, 10);
assert.strictEqual(late.read(3), 'éll');
assert.strictEqual(late.read(), 'o wörld');

const hex = new Readable({ read() {} });
hex.setEncoding('hex');
hex.push(Buffer.from([1, 2, 3]));
assert.strictEqual(hex.readableLength, 6);
assert.strictEqual(hex.read(4), '0102');
assert.strictEqual(hex.read(), '03');

const front = new Readable({ read() {} });
front.setEncoding('utf8');
front.push('xyz');
front.unshift('é');
assert.strictEqual(front.readableLength, 4);
assert.strictEqual(front.read(2), 'éx');
assert.strictEqual(front.read(), 'yz');

const partial = new Readable({ read() {} });
partial.push(Buffer.from('abcdef'));
assert.strictEqual(String(partial.read(2)), 'ab');
assert.strictEqual(partial._readableState.buffer.length >= 1, true);
assert.strictEqual(String(partial._readableState.buffer[partial._readableState.buffer.length - 1]), 'cdef');
partial.unshift(Buffer.from('Z'));
assert.strictEqual(String(partial.read()), 'Zcdef');

const tail = new Readable({ read() {} });
tail.setEncoding('utf8');
tail.push(euro.subarray(0, 2));
tail.push(null);
const chunks = [];
tail.on('data', (d) => chunks.push(d));
tail.on('end', () => {
  assert.deepStrictEqual(chunks, ['�']);

  const flowing = new Readable({ read() {} });
  flowing.setEncoding('utf8');
  const got = [];
  flowing.on('data', (d) => got.push(d));
  flowing.push(euro.subarray(0, 1));
  flowing.push(euro.subarray(1));
  flowing.push(null);
  flowing.on('end', () => {
    assert.deepStrictEqual(got, ['€']);
    console.log('readable setEncoding read: ok');
  });
});
