import assert from 'node:assert/strict';
import { Worker } from 'node:worker_threads';

const body = `
  const { parentPort, workerData } = require('worker_threads');
  const before = Ant.stats().intern.count;
  const parts = [];
  for (let i = 0; i < workerData; i++) parts.push('"worker_only_key_' + i + '":' + i);
  const parsed = JSON.parse('{' + parts.join(',') + '}');
  parentPort.postMessage({ keys: Object.keys(parsed).length, grew: Ant.stats().intern.count - before });
`;

const before = Ant.stats().intern;
for (let round = 0; round < 3; round++) {
  const worker = new Worker(body, { eval: true, workerData: 5000 });
  const result = await new Promise((resolve, reject) => { worker.once('message', resolve); worker.once('error', reject); });
  assert.equal(result.keys, 5000);
  assert.ok(result.grew >= 5000, `worker interned ${result.grew} keys`);
  assert.equal(await new Promise(r => worker.once('exit', r)), 0);
}

const after = Ant.stats().intern;
assert.ok(after.count - before.count < 64, `main isolate intern table grew by ${after.count - before.count}`);
assert.ok(!Object.hasOwn(JSON.parse('{"main_key":1}'), 'worker_only_key_0'));

console.log('intern per isolate: ok');
