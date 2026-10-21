import assert from 'node:assert/strict';
import { Readable, PassThrough } from 'node:stream';
import zlib from 'node:zlib';

assert.equal(typeof Readable.prototype[Symbol.asyncIterator], 'function');

{
  const seen = [];
  for await (const chunk of Readable.from(['a', 'b', 'c'])) seen.push(chunk);
  assert.deepEqual(seen, ['a', 'b', 'c']);
  const bytes = [];
  for await (const chunk of Readable.from(['x', 'y'], { objectMode: false })) bytes.push(chunk);
  assert.ok(bytes.every(Buffer.isBuffer));
  assert.equal(Buffer.concat(bytes).toString(), 'xy');
}

{
  const seen = [];
  for await (const chunk of Readable.from('whole')) seen.push(chunk);
  assert.deepEqual(seen, ['whole']);

  const buf = Buffer.from('bytes');
  const chunks = [];
  for await (const chunk of Readable.from(buf)) chunks.push(chunk);
  assert.equal(chunks.length, 1);
  assert.ok(Buffer.isBuffer(chunks[0]));
  assert.equal(chunks[0].toString(), 'bytes');
}

{
  const payload = Buffer.alloc(256 * 1024, 'x');
  const source = Readable.from([zlib.gzipSync(payload)]);
  const parts = [];
  for await (const chunk of source.pipe(zlib.createGunzip())) parts.push(chunk);
  assert.ok(parts.length > 1);
  assert.equal(Buffer.concat(parts).toString(), payload.toString());
}

{
  const stream = Readable.from(['1', '2', '3', '4']);
  const seen = [];
  for await (const chunk of stream) {
    seen.push(chunk);
    if (seen.length === 2) break;
  }
  assert.deepEqual(seen, ['1', '2']);
  assert.equal(stream.destroyed, true);
}

{
  const stream = new PassThrough();
  setTimeout(() => stream.destroy(new Error('boom')), 1);
  await assert.rejects(async () => {
    for await (const chunk of stream) assert.fail(`unexpected ${chunk}`);
  }, /boom/);
}

{
  const stream = new PassThrough({ objectMode: true });
  for (let i = 0; i < 64; i++) stream.write(i);
  stream.end();
  let sum = 0;
  for await (const n of stream) sum += n;
  assert.equal(sum, (63 * 64) / 2);
}

console.log('stream async iterator: ok');
