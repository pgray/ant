const assert = require('node:assert/strict');
const { MessageChannel } = require('node:worker_threads');

function roundtrip(value, transfer) {
  return new Promise(resolve => {
    const { port1, port2 } = new MessageChannel();
    port2.once('message', message => {
      port1.close();
      resolve(message);
    });
    port1.postMessage(value, transfer);
  });
}

async function main() {
  let setterHits = 0;
  Object.defineProperty(Object.prototype, 'trap', {
    set() { setterHits++; },
    get() { return 'inherited'; },
    configurable: true,
  });

  try {
    const evil = await roundtrip({ ...JSON.parse('{"__proto__": {"polluted": 1}}'), trap: 1 });
    assert.equal(Object.hasOwn(evil, '__proto__'), true);
    assert.deepEqual(Object.getOwnPropertyDescriptor(evil, '__proto__').value, { polluted: 1 });
    assert.equal(Object.getPrototypeOf(evil), Object.prototype);
    assert.equal(evil.polluted, undefined);
    assert.equal(Object.hasOwn(evil, 'trap'), true);
    assert.equal(evil.trap, 1);
    assert.equal(setterHits, 0);

    const nested = await roundtrip({ inner: { trap: 2, list: [{ trap: 3 }] } });
    assert.equal(nested.inner.trap, 2);
    assert.equal(nested.inner.list[0].trap, 3);
    assert.equal(setterHits, 0);
  } finally {
    delete Object.prototype.trap;
  }

  const keyed = await roundtrip({ b: 1, 2: 'two', a: 2, 1: 'one', '-1': 'neg', '01': 'lead' });
  assert.deepEqual(Object.keys(keyed), ['1', '2', 'b', 'a', '-1', '01']);
  assert.equal(keyed[1], 'one');
  assert.equal(keyed['01'], 'lead');

  const arr = [1, 2, 3];
  arr.extra = 'x';
  arr['-0'] = 'negzero';
  const arrCopy = await roundtrip(arr);
  assert.equal(Array.isArray(arrCopy), true);
  assert.deepEqual([...arrCopy], [1, 2, 3]);
  assert.equal(arrCopy.extra, 'x');
  assert.equal(arrCopy['-0'], 'negzero');
  assert.equal(arrCopy.length, 3);

  const desc = Object.getOwnPropertyDescriptor(await roundtrip({ v: 1 }), 'v');
  assert.deepEqual(desc, { value: 1, writable: true, enumerable: true, configurable: true });

  const errCopy = await roundtrip(new RangeError('bad', { cause: { trap: 'c' } }));
  assert.equal(errCopy instanceof RangeError, true);
  assert.equal(errCopy.message, 'bad');
  assert.deepEqual(errCopy.cause, { trap: 'c' });

  const shared = { s: 1 };
  const graph = await roundtrip({ a: shared, b: shared, self: null });
  assert.equal(graph.a, graph.b);

  const weird = await roundtrip({ '': 'empty', 'with space': 1, 'ünï': 2, constructor: 'c', hasOwnProperty: 'h' });
  assert.equal(weird[''], 'empty');
  assert.equal(weird['with space'], 1);
  assert.equal(weird['ünï'], 2);
  assert.equal(Object.hasOwn(weird, 'constructor'), true);
  assert.equal(weird.constructor, 'c');
  assert.equal(weird.hasOwnProperty, 'h');

  console.log('ok');
}

main();
