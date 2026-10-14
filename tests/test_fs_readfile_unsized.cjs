const fs = require('fs');
const os = require('os');
const path = require('path');
const { spawn, spawnSync } = require('child_process');

function assert(condition, message) {
  if (!condition) throw new Error(message);
}

function writeFifoLater(fifo, data) {
  return spawn('sh', ['-c', `sleep 0.2; printf '%s' "$1" > "$2"`, 'sh', data, fifo]);
}

async function main() {
  if (process.platform === 'linux') {
    const status = fs.readFileSync('/proc/self/status', 'utf8');
    assert(status.includes('Name:'), 'expected /proc/self/status contents');
  }

  const dir = fs.mkdtempSync(path.join(os.tmpdir(), 'ant-unsized-'));
  const fifo = path.join(dir, 'fifo');
  if (spawnSync('mkfifo', [fifo]).status !== 0) {
    console.log('fs.readFile unsized skipped (no mkfifo)');
    return;
  }

  try {
    writeFifoLater(fifo, 'sync-fifo-data');
    const syncData = fs.readFileSync(fifo, 'utf8');
    assert(syncData === 'sync-fifo-data', `readFileSync got ${JSON.stringify(syncData)}`);

    writeFifoLater(fifo, 'async-fifo-data');
    const asyncData = await fs.promises.readFile(fifo, 'utf8');
    assert(asyncData === 'async-fifo-data', `readFile got ${JSON.stringify(asyncData)}`);

    const big = 'x'.repeat(300000);
    const bigFile = path.join(dir, 'big');
    fs.writeFileSync(bigFile, big);
    assert(fs.readFileSync(bigFile, 'utf8') === big, 'readFileSync big mismatch');
    assert((await fs.promises.readFile(bigFile, 'utf8')) === big, 'readFile big mismatch');
  } finally {
    fs.rmSync(dir, { recursive: true, force: true });
  }

  console.log('fs.readFile unsized ok');
}

main().catch(e => { console.error(e); process.exit(1); });
