const assert = require('node:assert');
const { Readable, Writable, Transform, PassThrough, pipeline, finished } = require('node:stream');

const gc = () => {
  globalThis.gc?.();
  for (let i = 0; i < 2000; i++) void { i, s: 'x'.repeat(i % 32) };
};

async function queuedWritesSurviveGc() {
  const seen = [];
  const callbacks = [];
  let release;
  const w = new Writable({
    highWaterMark: 4,
    write(chunk, encoding, cb) {
      seen.push(chunk.toString());
      if (seen.length === 1) release = cb;
      else cb();
    },
  });

  for (let i = 0; i < 100; i++) w.write(`chunk-${i}-${'y'.repeat(i)}`, () => callbacks.push(i));
  gc();
  release();
  await new Promise((resolve) => w.end(resolve));

  assert.strictEqual(seen.length, 100);
  for (let i = 0; i < 100; i++) assert.strictEqual(seen[i], `chunk-${i}-${'y'.repeat(i)}`);
  assert.deepStrictEqual(callbacks, Array.from({ length: 100 }, (_, i) => i));
}

async function reentrantWritesDuringDrain() {
  const seen = [];
  let first = true;
  let release;
  const w = new Writable({
    write(chunk, encoding, cb) {
      seen.push(chunk.toString());
      if (first) {
        first = false;
        release = cb;
        return;
      }
      if (seen.length < 40) w.write(`re-${seen.length}`);
      cb();
    },
  });

  for (let i = 0; i < 20; i++) w.write(`w-${i}`);
  release();
  await new Promise((resolve) => w.end(resolve));
  assert.strictEqual(seen[0], 'w-0');
  assert.ok(seen.length >= 40);
}

async function writeErrorFailsQueuedCallbacks() {
  const errors = [];
  let release;
  const w = new Writable({ write(chunk, enc, cb) { release = cb; } });
  w.on('error', () => {});
  for (let i = 0; i < 10; i++) w.write(`q-${i}`, (err) => errors.push(err?.message));
  gc();
  release(new Error('boom'));
  await new Promise((resolve) => setTimeout(resolve, 10));
  assert.deepStrictEqual(errors, Array(10).fill('boom'));
}

async function pipeAndUnpipe() {
  const out = [];
  const src = new Readable({ read() {} });
  const dst = new Writable({ write(chunk, enc, cb) { out.push(chunk.toString()); cb(); } });
  src.pipe(dst);
  gc();
  src.push('a');
  src.push('b');
  await new Promise((resolve) => setImmediate(resolve));
  src.unpipe(dst);
  src.push('c');
  await new Promise((resolve) => setImmediate(resolve));
  assert.deepStrictEqual(out, ['a', 'b']);
}

async function pipelineAndFinished() {
  const out = [];
  const upper = new Transform({
    transform(chunk, enc, cb) { cb(null, chunk.toString().toUpperCase()); },
    flush(cb) { cb(null, '!'); },
  });
  const sink = new Writable({ write(chunk, enc, cb) { out.push(chunk.toString()); cb(); } });
  await new Promise((resolve, reject) => {
    pipeline(Readable.from(['x', 'y']), upper, new PassThrough(), sink, (err) => (err ? reject(err) : resolve()));
    gc();
  });
  assert.deepStrictEqual(out, ['X', 'Y', '!']);

  let calls = 0;
  const r = new Readable({ read() {} });
  finished(r, () => calls++);
  r.resume();
  r.push(null);
  await new Promise((resolve) => setTimeout(resolve, 10));
  r.emit('close');
  assert.strictEqual(calls, 1);
}

async function asyncIteratorBackpressure() {
  const r = new Readable({ objectMode: true, read() {} });
  for (let i = 0; i < 50; i++) r.push(i);
  r.push(null);
  const got = [];
  for await (const v of r) {
    got.push(v);
    if (v % 10 === 0) gc();
  }
  assert.deepStrictEqual(got, Array.from({ length: 50 }, (_, i) => i));

  const e = new Readable({ read() {} });
  const it = e[Symbol.asyncIterator]();
  const pending = it.next();
  e.destroy(new Error('iter-fail'));
  await assert.rejects(pending, /iter-fail/);
}

async function readableFromSources() {
  const fromIter = [];
  for await (const v of Readable.from((function* () { yield 1; yield 2; })())) fromIter.push(v);
  assert.deepStrictEqual(fromIter, [1, 2]);

  const fromAsync = [];
  for await (const v of Readable.from((async function* () { yield 'a'; gc(); yield 'b'; })())) fromAsync.push(v);
  assert.deepStrictEqual(fromAsync, ['a', 'b']);

  const failing = Readable.from((async function* () { yield 1; throw new Error('from-fail'); })());
  await assert.rejects(async () => { for await (const _ of failing); }, /from-fail/);
}

async function toWeb() {
  const r = new Readable({ read() {} });
  const reader = Readable.toWeb(r).getReader();
  r.push('p');
  gc();
  r.push(null);
  const first = await reader.read();
  assert.strictEqual(Buffer.from(first.value).toString(), 'p');
  assert.strictEqual((await reader.read()).done, true);
}

(async () => {
  await queuedWritesSurviveGc();
  await reentrantWritesDuringDrain();
  await writeErrorFailsQueuedCallbacks();
  await pipeAndUnpipe();
  await pipelineAndFinished();
  await asyncIteratorBackpressure();
  await readableFromSources();
  await toWeb();
  console.log('stream native op state: ok');
})().catch((err) => {
  console.error(err);
  process.exit(1);
});
