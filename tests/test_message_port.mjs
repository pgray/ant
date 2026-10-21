import assert from 'node:assert/strict';
import { receiveMessageOnPort, markAsUntransferable, isMarkedAsUntransferable } from 'node:worker_threads';

const tick = () => new Promise(r => setTimeout(r, 10));
const next = port => new Promise(r => port.once('message', r));

{
  const { port1, port2 } = new MessageChannel();
  const order = [];
  port2.on('message', v => order.push(`on:${typeof v === 'object' ? JSON.stringify(v) : v}`));
  port2.addEventListener('message', e => order.push(`ael:${e.constructor.name}`));
  port1.postMessage('a');
  port1.postMessage({ b: 1 });
  assert.deepEqual(order, []);
  await tick();
  assert.deepEqual(order, ['on:a', 'ael:MessageEvent', 'on:{"b":1}', 'ael:MessageEvent']);
  port1.close();
}

{
  const { port1, port2 } = new MessageChannel();
  const sab = new SharedArrayBuffer(8);
  const view = new Int32Array(sab);
  const cyc = { name: 'cyc' };
  cyc.self = cyc;
  const ab = new ArrayBuffer(4);
  new Uint8Array(ab).set([1, 2, 3, 4]);
  const err = new RangeError('boom', { cause: new TypeError('inner') });
  const value = {
    m: new Map([[1, 'one'], ['k', { v: 2 }]]), s: new Set([1, 'x']), d: new Date(1700000000000),
    re: /a+b/gi, err, big: 12345678901234567890n, boxed: [Object(1), Object('s'), Object(false)], cyc,
    u8: new Uint8Array([9, 8, 7]), dv: new DataView(new ArrayBuffer(2)), view, ab, neg0: -0, nan: NaN,
  };
  const pending = next(port2);
  port1.postMessage(value, [ab]);
  const got = await pending;

  assert.equal(ab.byteLength, 0);
  assert.ok(got.m instanceof Map);
  assert.equal(got.m.get('k').v, 2);
  assert.deepEqual([...got.s], [1, 'x']);
  assert.equal(got.d.getTime(), 1700000000000);
  assert.equal(got.re.source, 'a+b');
  assert.equal(got.re.flags, 'gi');
  assert.ok(got.err instanceof RangeError);
  assert.equal(got.err.message, 'boom');
  assert.ok(got.err.cause instanceof TypeError);
  assert.equal(typeof got.err.stack, 'string');
  assert.equal(got.big, 12345678901234567890n);
  assert.ok(got.boxed[0] instanceof Number && got.boxed[1] instanceof String);
  assert.equal(got.boxed[2].valueOf(), false);
  assert.equal(got.cyc.self, got.cyc);
  assert.deepEqual([...got.u8], [9, 8, 7]);
  assert.ok(got.dv instanceof DataView);
  got.view[0] = 7;
  assert.equal(view[0], 7);
  assert.deepEqual([...new Uint8Array(got.ab)], [1, 2, 3, 4]);
  assert.ok(Object.is(got.neg0, -0) && Number.isNaN(got.nan));

  for (const bad of [() => {}, Symbol('q'), new WeakMap(), Promise.resolve()])
    assert.throws(() => port1.postMessage(bad), { name: 'DataCloneError' });
  assert.throws(() => port1.postMessage(port2), { name: 'DataCloneError' });
  assert.throws(() => port1.postMessage(1, [port1]), { name: 'DataCloneError' });

  const { port1: a1, port2: a2 } = new MessageChannel();
  const movedP = next(port2);
  port1.postMessage({ p: a2 }, [a2]);
  const moved = (await movedP).p;
  const echoP = next(moved);
  a1.postMessage('via moved');
  assert.equal(await echoP, 'via moved');

  let closes = 0;
  a1.on('close', () => closes++);
  moved.on('close', () => closes++);
  a1.close();
  await tick();
  assert.equal(closes, 2);
  port1.close();
}

{
  const { port1, port2 } = new MessageChannel();
  port1.postMessage({ a: 1 });
  port1.postMessage({ b: 2 });
  await tick();
  assert.deepEqual(receiveMessageOnPort(port2), { message: { a: 1 } });
  assert.deepEqual(receiveMessageOnPort(port2), { message: { b: 2 } });
  assert.equal(receiveMessageOnPort(port2), undefined);
  port1.close();
}

{
  const ab = new ArrayBuffer(8);
  markAsUntransferable(ab);
  assert.equal(isMarkedAsUntransferable(ab), true);
  const { port1 } = new MessageChannel();
  assert.throws(() => port1.postMessage(ab, [ab]), { name: 'DataCloneError' });
  assert.equal(ab.byteLength, 8);
  port1.close();
}

{
  const bc1 = new BroadcastChannel('test-message-port');
  const bc2 = new BroadcastChannel('test-message-port');
  const got = await new Promise(r => { bc2.onmessage = e => r(e.data); bc1.postMessage({ hi: 1 }); });
  assert.deepEqual(got, { hi: 1 });
  bc1.close();
  bc2.close();
}

console.log('message port: ok');
