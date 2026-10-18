const zlib = require('node:zlib');
const invalid = Buffer.from('invalid gzip');

function assert(condition, message) {
  if (!condition) throw new Error(message);
}

const reasons = [new Error('zlib listener'), undefined, null, false, 0, 'listener'];
const uncaught = [];
process.on('uncaughtException', error => uncaught.push(error));

let queued = false;
queueMicrotask(() => { queued = true; });

for (const reason of reasons) {
  for (const method of ['write', 'end']) {
    const stream = zlib.createGunzip();
    let errors = 0;
    stream.on('error', error => {
      errors++;
      assert(error instanceof Error, method + ': processing error changed');
      throw reason;
    });
    let threw = false;
    try {
      if (method === 'write') stream.write(invalid);
      else stream.end(invalid);
    } catch {
      threw = true;
    }
    assert(!threw, method + ': listener throw escaped synchronously');
    stream.on('close', () => assert(errors === 1, method + ': listener did not run once'));
  }
}

const handled = [];
for (const method of ['write', 'end']) {
  const stream = zlib.createGunzip();
  stream.on('error', error => {
    assert(error instanceof Error, method + ': handled processing error changed');
    handled.push(method);
  });
  if (method === 'write') stream.write(invalid);
  else stream.end(invalid);
}

const stream = zlib.createGzip();
const output = [];
let flushed = false;
stream.on('data', chunk => output.push(chunk));
assert(stream.write('successful compression') === true, 'successful write return changed');
stream.flush(error => {
  assert(error === null, 'successful flush callback error changed');
  flushed = true;
});
assert(!flushed, 'successful flush callback must remain asynchronous');
let ended = false;
stream.end(() => { ended = true; });

setTimeout(() => {
  process.removeAllListeners('uncaughtException');
  assert(queued, 'queued work was dropped');
  assert(flushed, 'successful flush callback was dropped');
  assert(ended, 'successful end callback was dropped');
  assert(zlib.gunzipSync(Buffer.concat(output)).toString() === 'successful compression', 'successful compression changed');
  assert(handled.length === 2, 'handled processing errors were not reported once each');
  assert(uncaught.length === reasons.length * 2, `listener throws reported ${uncaught.length} times`);
  for (const reason of reasons) {
    const seen = uncaught.filter(error => Object.is(error, reason)).length;
    assert(seen === 2, `listener throw ${String(reason)} reported ${seen} times`);
  }
  console.log('PASS');
}, 50);
