const assert = require('node:assert/strict');
const fs = require('node:fs');
const os = require('node:os');
const path = require('node:path');
const stream = require('node:stream');
const { Readable, Writable, Duplex, PassThrough, Transform } = stream;

const tick = (ms = 30) => new Promise(r => setTimeout(r, ms));

async function track(s, drive) {
  const events = [];
  for (const e of ['end', 'finish', 'close']) s.on(e, () => events.push(e));
  s.on('error', () => {});
  await drive(s);
  await tick();
  return { events, destroyed: s.destroyed };
}

async function writeError(s) {
  return new Promise(resolve => s.write('late', err => resolve(err)));
}

async function main() {
  const file = path.join(os.tmpdir(), `ant-stream-lifecycle-${process.pid}.txt`);
  fs.writeFileSync(file, 'hello');

  assert.deepEqual(await track(new Readable({ read() {} }), s => { s.resume(); s.push('a'); s.push(null); }),
    { events: ['end', 'close'], destroyed: true });
  assert.deepEqual(await track(new Readable({ read() {}, autoDestroy: false }), s => { s.resume(); s.push(null); }),
    { events: ['end'], destroyed: false });
  assert.deepEqual(await track(Readable.from(['a', 'b']), s => s.resume()),
    { events: ['end', 'close'], destroyed: true });
  assert.deepEqual(await track(new Writable({ write(c, e, cb) { cb(); } }), s => s.end('x')),
    { events: ['finish', 'close'], destroyed: true });
  assert.deepEqual(await track(new Duplex({ read() {}, write(c, e, cb) { cb(); } }), s => { s.resume(); s.push(null); }),
    { events: ['end'], destroyed: false });
  assert.deepEqual(await track(new Duplex({ allowHalfOpen: false, read() {}, write(c, e, cb) { cb(); } }), s => { s.resume(); s.push(null); }),
    { events: ['end', 'finish', 'close'], destroyed: true });
  assert.deepEqual(await track(new PassThrough(), s => { s.resume(); s.end('x'); }),
    { events: ['end', 'finish', 'close'], destroyed: true });
  assert.deepEqual(await track(new Transform({ transform(c, e, cb) { cb(null, c); } }), s => { s.resume(); s.end('x'); }),
    { events: ['end', 'finish', 'close'], destroyed: true });
  assert.deepEqual(await track(fs.createReadStream(file), s => s.resume()),
    { events: ['end', 'close'], destroyed: true });
  assert.deepEqual(await track(fs.createWriteStream(file + '.out'), s => s.end('x')),
    { events: ['finish', 'close'], destroyed: true });
  assert.deepEqual(await track(new Readable({ read() {} }), s => { s.resume(); s.push('a'); s.destroy(); }),
    { events: ['close'], destroyed: true });

  const destroyed = new PassThrough();
  destroyed.on('error', () => {});
  destroyed.destroy();
  await tick();
  const destroyedErr = await writeError(destroyed);
  assert.equal(destroyedErr.code, 'ERR_STREAM_DESTROYED');
  assert.equal(destroyedErr.message, 'Cannot call write after a stream was destroyed');

  const ended = new PassThrough();
  ended.on('error', () => {});
  await new Promise(r => { ended.resume(); ended.on('end', r); ended.end('x'); });
  const endedErr = await writeError(ended);
  assert.equal(endedErr.code, 'ERR_STREAM_WRITE_AFTER_END');
  assert.equal(endedErr.message, 'write after end');

  let sync = true;
  const later = new PassThrough();
  later.on('error', () => {});
  later.destroy();
  const order = await new Promise(resolve => { later.write('x', () => resolve(sync)); sync = false; });
  assert.equal(order, false);

  const quiet = new Readable({ read() {} });
  const quietEnded = new Promise(r => quiet.once('end', r));
  quiet.resume();
  quiet.push(null);
  await quietEnded;
  await tick();
  assert.equal(quiet.destroyed, true);
  assert.equal(stream.isDisturbed(quiet), false);

  const aborted = new Readable({ read() {} });
  aborted.destroy();
  assert.equal(stream.isDisturbed(aborted), true);

  fs.rmSync(file, { force: true });
  fs.rmSync(file + '.out', { force: true });
  console.log('ok');
}

main();
