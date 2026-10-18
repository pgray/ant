const assert = require('node:assert/strict');
const { spawnSync } = require('node:child_process');

const run = (...args) => spawnSync(process.execPath, args, { encoding: 'utf8' });

for (const bad of ['4', 'abc', '', '131073']) {
  const r = run(`--cage-size=${bad}`, '-e', '1');
  assert.equal(r.status, 1, `--cage-size=${bad}`);
  assert.match(r.stderr, /--cage-size must be a whole number of GiB/);
}

const workers = `
const { Worker } = require('worker_threads');
const n = Number(process.argv[1]);
const body = 'const { parentPort } = require("worker_threads"); parentPort.once("message", () => parentPort.close()); parentPort.postMessage(1);';
Promise.all(Array.from({ length: n }, () => new Promise(resolve => {
  const w = new Worker(body, { eval: true });
  w.once('message', () => resolve({ w }));
  w.once('error', e => resolve({ code: e.code }));
}))).then(r => {
  console.log(JSON.stringify(r.filter(x => !x.w).map(x => x.code)));
  for (const x of r) x.w?.postMessage('bye');
});
`;

const small = run('--cage-size=8', '-e', workers, '8');
assert.equal(small.status, 0, small.stderr);
const failures = JSON.parse(small.stdout.trim());
assert.ok(failures.length > 0);
assert.ok(failures.every(code => code === 'ERR_WORKER_INIT_FAILED'));

const large = run('--cage-size=64', '-e', workers, '48');
assert.equal(large.status, 0, large.stderr);
assert.deepEqual(JSON.parse(large.stdout.trim()), []);

console.log('cage size flag: ok');
