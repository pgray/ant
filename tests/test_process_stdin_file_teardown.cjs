const assert = require('node:assert');
const { spawnSync } = require('node:child_process');
const fs = require('node:fs');
const os = require('node:os');
const path = require('node:path');

const tmp = fs.mkdtempSync(path.join(os.tmpdir(), 'ant-stdin-file-'));
const input = path.join(tmp, 'big.txt');
fs.writeFileSync(input, 'x'.repeat(4 * 1024 * 1024));

function run(script) {
  const fd = fs.openSync(input, 'r');
  try {
    return spawnSync(process.execPath, ['-e', script], { stdio: [fd, 'pipe', 'pipe'], encoding: 'utf8' });
  } finally {
    fs.closeSync(fd);
  }
}

try {
  const destroyed = run('process.stdin.once("data", () => { process.stdin.destroy(); console.log("destroyed"); });');
  assert.strictEqual(destroyed.status, 0, destroyed.stderr);
  assert.strictEqual(destroyed.stdout, 'destroyed\n');

  const exited = run('process.stdin.once("data", () => process.exit(3));');
  assert.strictEqual(exited.status, 3, exited.stderr);

  const drained = run('let n = 0; process.stdin.on("data", d => n += d.length).on("end", () => console.log(n));');
  assert.strictEqual(drained.status, 0, drained.stderr);
  assert.strictEqual(drained.stdout, `${4 * 1024 * 1024}\n`);
} finally {
  fs.rmSync(tmp, { recursive: true, force: true });
}

console.log('ok');
