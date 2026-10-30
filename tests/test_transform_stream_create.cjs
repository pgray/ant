const assert = require('node:assert');

assert.throws(
  () => new TransformStream({ get readableType() { throw new SyntaxError('getter'); } }),
  { name: 'SyntaxError', message: 'getter' }
);

assert.throws(() => new TransformStream({ transform: 1 }), TypeError);
assert.throws(() => new TransformStream({}, { size: 1 }), TypeError);
assert.throws(() => new TransformStream({}, undefined, { highWaterMark: -1 }), RangeError);

async function main() {
  let started = false;
  const thenable = { then(resolve) { setTimeout(() => { started = true; resolve(); }, 10); } };
  const ts = new TransformStream({
    start() { return thenable; },
    transform(chunk, ctl) { ctl.enqueue(`${chunk}:${started}`); },
  });

  const writer = ts.writable.getWriter();
  writer.write('x');
  writer.close();

  const chunks = [];
  for await (const chunk of ts.readable) chunks.push(chunk);
  assert.deepStrictEqual(chunks, ['x:true']);

  const rejected = new TransformStream({ start() { return Promise.reject(new Error('start')); } });
  await assert.rejects(rejected.readable.getReader().read(), { message: 'start' });

  console.log('ok');
}

main();
