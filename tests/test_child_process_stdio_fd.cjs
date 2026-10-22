const assert = require('node:assert');
const { spawn, spawnSync } = require('node:child_process');
const fs = require('node:fs');
const os = require('node:os');
const path = require('node:path');

const tmp = fs.mkdtempSync(path.join(os.tmpdir(), 'ant-stdio-fd-'));
const input = path.join(tmp, 'in.txt');
const output = path.join(tmp, 'out.txt');
const text = 'hello from a file\n';
fs.writeFileSync(input, text);

const cat = ['-e', 'process.stdin.pipe(process.stdout)'];

function withFd(file, flags, fn) {
  const fd = fs.openSync(file, flags);
  try {
    return fn(fd);
  } finally {
    fs.closeSync(fd);
  }
}

function cleanup() {
  fs.rmSync(tmp, { recursive: true, force: true });
}

try {
  withFd(input, 'r', fd => {
    const r = spawnSync(process.execPath, cat, { stdio: [fd, 'pipe', 'pipe'], encoding: 'utf8' });
    assert.strictEqual(r.status, 0, r.stderr);
    assert.strictEqual(r.stdout, text);
  });

  withFd(input, 'r', fd => {
    const r = spawnSync(process.execPath, cat, { stdio: [{ fd }, 'pipe', 'pipe'], encoding: 'utf8' });
    assert.strictEqual(r.stdout, text);
  });

  withFd(input, 'r', inFd => withFd(output, 'w', outFd => {
    const r = spawnSync(process.execPath, cat, { stdio: [inFd, outFd, 'pipe'] });
    assert.strictEqual(r.status, 0);
  }));
  assert.strictEqual(fs.readFileSync(output, 'utf8'), text);

  const bad = spawnSync(process.execPath, cat, { stdio: [987654, 'pipe', 'pipe'] });
  assert.strictEqual(bad.error && bad.error.code, 'EBADF');

  const fd = fs.openSync(input, 'r');
  const child = spawn(process.execPath, cat, { stdio: [fd, 'pipe', 'inherit'] });
  let out = '';
  child.stdout.setEncoding('utf8');
  child.stdout.on('data', d => { out += d; });
  child.on('close', code => {
    fs.closeSync(fd);
    cleanup();
    assert.strictEqual(code, 0);
    assert.strictEqual(out, text);
    console.log('ok');
  });
} catch (error) {
  cleanup();
  throw error;
}
