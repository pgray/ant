const assert = require('node:assert/strict');
const zlib = require('node:zlib');

const tick = (ms = 20) => new Promise(r => setTimeout(r, ms));

function collect(z, input) {
  return new Promise((resolve, reject) => {
    const chunks = [];
    z.on('data', c => chunks.push(c));
    z.on('error', reject);
    z.on('end', () => resolve(Buffer.concat(chunks)));
    if (input === undefined) z.end();
    else z.end(input);
  });
}

async function main() {
  for (const make of [zlib.createGzip, zlib.createDeflate, zlib.createInflate, zlib.createBrotliCompress]) {
    const z = make();
    assert.equal(z._handle.close(), undefined);
    assert.equal(z.destroyed, false);
    z._handle.close();
    const closed = new Promise(resolve => z.on('close', resolve));
    z.destroy();
    await closed;
    assert.equal(z.destroyed, true);
    assert.equal(z._handle, null);
  }

  const closedByApi = zlib.createGzip();
  await new Promise(resolve => closedByApi.close(resolve));
  assert.equal(closedByApi._handle, null);
  assert.equal(await new Promise(resolve => closedByApi.flush(() => resolve('flushed'))), 'flushed');

  assert.equal(zlib.gunzipSync(await collect(zlib.createGzip())).length, 0);
  assert.equal(zlib.inflateSync(await collect(zlib.createDeflate())).length, 0);
  assert.equal(zlib.inflateRawSync(await collect(zlib.createDeflateRaw())).length, 0);

  const text = 'lazy deflate init '.repeat(200);
  const flushedFirst = zlib.createDeflate();
  await new Promise(resolve => flushedFirst.flush(resolve));
  assert.equal(zlib.inflateSync(await collect(flushedFirst, text)).toString(), text);

  const tuned = zlib.createGzip();
  await new Promise(resolve => tuned.params(1, zlib.constants.Z_DEFAULT_STRATEGY, resolve));
  assert.equal(zlib.gunzipSync(await collect(tuned, text)).toString(), text);

  const resetFirst = zlib.createGzip();
  resetFirst.reset();
  assert.equal(zlib.gunzipSync(await collect(resetFirst, text)).toString(), text);

  const dict = Buffer.from('lazy deflate init dictionary');
  assert.equal(typeof zlib.createGzip({ dictionary: dict }).write, 'function');
  assert.equal(zlib.gunzipSync(zlib.gzipSync(text, { dictionary: dict })).toString(), text);
  const withDict = await collect(zlib.createDeflate({ dictionary: dict }), text);
  assert.equal(zlib.inflateSync(withDict, { dictionary: dict }).toString(), text);
  assert.equal((await collect(zlib.createInflate({ dictionary: dict }), withDict)).toString(), text);
  const rawDict = await collect(zlib.createDeflateRaw({ dictionary: dict }), text);
  assert.equal((await collect(zlib.createInflateRaw({ dictionary: dict }), rawDict)).toString(), text);

  const roundtrip = await collect(zlib.createGunzip(), await collect(zlib.createGzip(), text));
  assert.equal(roundtrip.toString(), text);

  const bytewise = zlib.createInflate({ dictionary: dict });
  const bytewiseOut = new Promise((resolve, reject) => {
    const chunks = [];
    bytewise.on('data', c => chunks.push(c));
    bytewise.on('error', reject);
    bytewise.on('end', () => resolve(Buffer.concat(chunks).toString()));
  });
  for (const b of withDict) bytewise.write(Buffer.from([b]));
  bytewise.end();
  assert.equal(await bytewiseOut, text);

  await assert.rejects(collect(zlib.createInflate(), withDict), { code: 'Z_NEED_DICT', message: 'Missing dictionary' });
  await assert.rejects(collect(zlib.createGunzip(), Buffer.from('not compressed at all')),
    { code: 'Z_DATA_ERROR', errno: zlib.constants.Z_DATA_ERROR, message: 'incorrect header check' });

  if (typeof Ant !== 'undefined') {
    const z = zlib.createGzip();
    z._handle.close();
    const errored = new Promise(resolve => z.on('error', resolve));
    const writeErr = await new Promise(resolve => z.write('after close', resolve));
    assert.equal(writeErr.code, 'ERR_ZLIB_BINDING_CLOSED');
    assert.equal((await errored).code, 'ERR_ZLIB_BINDING_CLOSED');
    await tick();
    assert.equal(z.destroyed, true);

    const sync = zlib.createDeflate();
    sync._handle.close();
    assert.throws(() => sync._processChunk(Buffer.from('x'), zlib.constants.Z_FINISH), { code: 'ERR_ZLIB_BINDING_CLOSED' });
    assert.throws(() => sync.params(1, 0, () => {}), { code: 'ERR_ZLIB_BINDING_CLOSED' });
  }

  console.log('ok');
}

main();
