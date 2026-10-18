import assert from 'node:assert/strict';
import { Readable, Writable, Duplex, Transform, PassThrough, finished, pipeline } from 'node:stream';
import zlib from 'node:zlib';

for (const make of [zlib.createGzip, zlib.createGunzip, zlib.createDeflate, zlib.createInflate, zlib.createBrotliCompress]) {
  const s = make();
  assert.ok(s instanceof Transform, make.name);
  assert.ok(s instanceof Duplex, make.name);
  assert.ok(s instanceof Readable, make.name);
  assert.ok(s instanceof Writable, make.name);
  s.destroy();
}

assert.ok(new Duplex() instanceof Writable);
assert.ok(new PassThrough() instanceof Writable);
assert.ok(!(new Readable() instanceof Writable));
class Sink extends Writable {}
assert.ok(new Sink() instanceof Writable);
assert.ok(!(new Duplex() instanceof Sink));

{
  const gz = zlib.createGzip();
  assert.equal(gz.writable, true);
  assert.equal(gz.writableEnded, false);
  assert.equal(gz.writableFinished, false);
  const chunks = [];
  gz.on('data', c => chunks.push(c));
  await new Promise((resolve, reject) => {
    finished(gz, err => (err ? reject(err) : resolve()));
    gz.end('hello');
  });
  assert.equal(gz.writable, false);
  assert.equal(gz.writableEnded, true);
  assert.equal(gz.writableFinished, true);
  assert.equal(zlib.gunzipSync(Buffer.concat(chunks)).toString(), 'hello');
}

{
  const out = [];
  await new Promise((resolve, reject) => pipeline(
    Readable.from(['a', 'b', 'c']),
    zlib.createGzip(),
    zlib.createGunzip(),
    new Writable({ write(chunk, enc, cb) { out.push(chunk); cb(); } }),
    err => (err ? reject(err) : resolve()),
  ));
  assert.equal(Buffer.concat(out).toString(), 'abc');
}

{
  const raw = Buffer.alloc(4 * 1024 * 1024);
  for (let i = 0; i < raw.length; i++) raw[i] = (i * 31 + (i >> 9)) & 0xff;
  const gz = zlib.gzipSync(raw, { level: 1 });
  const chunks = [];
  for (let i = 0; i < gz.length; i += 64 * 1024) chunks.push(gz.subarray(i, i + 64 * 1024));
  let total = 0;
  await new Promise((resolve, reject) => {
    Readable.from(chunks)
      .pipe(zlib.createGunzip())
      .on('error', reject)
      .pipe(new Writable({ write(c, e, cb) { total += c.length; cb(); } }))
      .on('finish', resolve)
      .on('error', reject);
  });
  assert.equal(total, raw.length);
}

{
  const gz = zlib.createGzip();
  const big = Buffer.alloc(64 * 1024, 1);
  assert.equal(gz.write(big), false);
  await new Promise(resolve => gz.once('drain', resolve));
  gz.resume();
  gz.end();
}

{
  const d = zlib.createDeflate();
  d.destroy();
  assert.equal(d.writable, false);
  assert.equal(d.destroyed, true);
}

console.log('zlib stream instanceof: ok');
