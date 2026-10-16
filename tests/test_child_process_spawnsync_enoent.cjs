const { spawnSync, execFileSync } = require('child_process');

function assert(condition, message) {
  if (!condition) throw new Error(message);
}

const missing = 'ant-definitely-missing-binary';
const result = spawnSync(missing, ['a', 'b']);

assert(result.status === null, `expected null status, got ${result.status}`);
assert(result.signal === null, `expected null signal, got ${result.signal}`);
assert(result.pid === 0, `expected pid 0, got ${result.pid}`);
assert(result.output === null, `expected null output, got ${result.output}`);
assert(result.error instanceof Error, 'expected an error');
assert(result.error.code === 'ENOENT', `expected ENOENT, got ${result.error.code}`);
assert(result.error.syscall === `spawnSync ${missing}`, `bad syscall ${result.error.syscall}`);
assert(result.error.path === missing, `bad path ${result.error.path}`);
assert(JSON.stringify(result.error.spawnargs) === '["a","b"]', `bad spawnargs ${result.error.spawnargs}`);

const badCwd = spawnSync('ls', { cwd: '/ant-definitely-missing-dir' });
assert(badCwd.error && badCwd.error.code === 'ENOENT', `expected cwd ENOENT, got ${badCwd.error && badCwd.error.code}`);

assert(Object.getPrototypeOf(result) === Object.prototype, 'spawnSync error result should inherit Object.prototype');
assert(Object.getPrototypeOf(spawnSync('true')) === Object.prototype, 'spawnSync result should inherit Object.prototype');

const exit127 = spawnSync('sh', ['-c', 'exit 127']);
assert(exit127.status === 127 && !exit127.error, 'real exit 127 should not be an error');

let thrown;
try { execFileSync(missing); } catch (e) { thrown = e; }
assert(thrown && thrown.code === 'ENOENT', `execFileSync should throw ENOENT, got ${thrown && thrown.code}`);

console.log('child_process.spawnSync ENOENT ok');
