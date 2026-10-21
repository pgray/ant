const assert = require('node:assert/strict');
const { Worker } = require('node:worker_threads');

const zero = { idle: 0, active: 0, utilization: 0 };
const body = `
  const { parentPort } = require('worker_threads');
  const end = Date.now() + 50; while (Date.now() < end) {}
  parentPort.on('message', m => { if (m === 'bye') parentPort.close(); });
  parentPort.postMessage('ready');
`;

const w = new Worker(body, { eval: true });
assert.deepEqual(Object.keys(w.performance), ['eventLoopUtilization']);
assert.deepEqual(w.performance.eventLoopUtilization(), zero);

const detached = w.performance.eventLoopUtilization;

w.once('message', () => {
  setTimeout(() => {
    const a = detached();
    assert.deepEqual(Object.keys(a), ['idle', 'active', 'utilization']);
    assert.ok(a.idle > 0 && a.active >= 0);
    assert.ok(a.utilization >= 0 && a.utilization <= 1);

    setTimeout(() => {
      const b = w.performance.eventLoopUtilization();
      assert.ok(b.idle >= a.idle);
      const since = w.performance.eventLoopUtilization(a);
      assert.ok(since.idle > 0);
      const pair = w.performance.eventLoopUtilization(b, a);
      assert.equal(pair.idle, b.idle - a.idle);
      assert.equal(pair.active, b.active - a.active);
      w.postMessage('bye');
    }, 40);
  }, 40);
});

w.on('exit', code => {
  assert.equal(code, 0);
  assert.deepEqual(w.performance.eventLoopUtilization(), zero);
  console.log('worker performance: ok');
});
