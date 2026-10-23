const assert = require('node:assert');
const { execFile } = require('node:child_process');

const argv = ['small'];
let reads = 0;
Object.defineProperty(argv, 0, { get: () => (reads++ < 2 ? 'small' : 'B'.repeat(1 << 20)) });

execFile('/bin/echo', argv, (err, stdout) => {
  assert.ifError(err);
  assert.ok(stdout === 'small\n' || stdout.length === (1 << 20) + 1, `unexpected stdout length ${stdout.length}`);
  console.log('child_process execFile argv getter test passed');
});
