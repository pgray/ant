const assert = require('node:assert');
const fs = require('node:fs');
const os = require('node:os');
const path = require('node:path');

const tmpDir = fs.mkdtempSync(path.join(os.tmpdir(), 'ant-writefile-fd-'));

try {
  const file = path.join(tmpDir, 'session.jsonl');
  const fd = fs.openSync(file, 'wx');
  try {
    fs.writeFileSync(fd, 'a\n');
    fs.writeFileSync(fd, Buffer.from('b\n'));
    fs.appendFileSync(fd, 'c\n');
  } finally {
    fs.closeSync(fd);
  }
  assert.strictEqual(fs.readFileSync(file, 'utf8'), 'a\nb\nc\n');
  assert.throws(() => fs.openSync(file, 'wx'), { code: 'EEXIST' });

  const reused = fs.openSync(file, 'r+');
  fs.writeFileSync(reused, 'xy');
  assert.strictEqual(fs.fstatSync(reused).size, 6);
  fs.closeSync(reused);
  assert.strictEqual(fs.readFileSync(file, 'utf8'), 'xyb\nc\n');

  assert.throws(() => fs.writeFileSync(-1, 'x'));
} finally {
  fs.rmSync(tmpDir, { recursive: true, force: true });
}

console.log('fs writeFileSync fd: ok');
