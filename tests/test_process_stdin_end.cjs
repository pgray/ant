const assert = require('node:assert');
const { spawn, spawnSync } = require('node:child_process');
const fs = require('node:fs');
const os = require('node:os');
const path = require('node:path');

const payload = 'x'.repeat(200000) + '\n';
const tmp = fs.mkdtempSync(path.join(os.tmpdir(), 'ant-stdin-end-'));
const inputFile = path.join(tmp, 'input.txt');
const emptyFile = path.join(tmp, 'empty.txt');
fs.writeFileSync(inputFile, payload);
fs.writeFileSync(emptyFile, '');

const readers = {
  data: `
    let n = 0;
    process.stdin.on('data', d => { n += d.length; });
    process.stdin.on('end', () => console.log('end', n));
    process.stdin.on('close', () => console.log('close'));
  `,
  encoding: `
    let s = '';
    process.stdin.setEncoding('utf8');
    process.stdin.on('data', d => { if (typeof d !== 'string') throw new Error('not a string'); s += d; });
    process.stdin.on('end', () => console.log('end', s.length));
  `,
  readable: `
    let n = 0;
    process.stdin.on('readable', () => { let c; while ((c = process.stdin.read()) !== null) n += c.length; });
    process.stdin.on('end', () => console.log('end', n));
  `,
  iterate: `
    (async () => {
      let n = 0;
      for await (const c of process.stdin) n += c.length;
      console.log('end', n);
    })();
  `,
  readFileSync: `
    console.log('end', require('node:fs').readFileSync(0, 'utf8').length);
  `,
};

function check(result) {
  assert.strictEqual(result.error, undefined, String(result.error));
  assert.strictEqual(result.status, 0, result.stderr);
  return result.stdout;
}

function run(code, input) {
  return check(spawnSync(process.execPath, ['-e', code], { input, encoding: 'utf8', timeout: 10000 }));
}

function runFromFile(code, file) {
  const fd = fs.openSync(file, 'r');
  try {
    return check(spawnSync(process.execPath, ['-e', code], { stdio: [fd, 'pipe', 'pipe'], encoding: 'utf8', timeout: 10000 }));
  } finally {
    fs.closeSync(fd);
  }
}

try {
  for (const [name, code] of Object.entries(readers)) {
    assert.match(run(code, payload), new RegExp(`^end ${payload.length}\\n`), `${name} over a pipe`);
    assert.match(run(code, ''), /^end 0\n/, `${name} over an empty pipe`);
    if (process.platform === 'win32') continue;
    assert.match(runFromFile(code, inputFile), new RegExp(`^end ${payload.length}\\n`), `${name} from a file`);
    assert.match(runFromFile(code, emptyFile), /^end 0\n/, `${name} from an empty file`);
  }

  assert.match(run(readers.data, payload), /\nclose\n/);

  const piped = run('process.stdin.pipe(process.stdout)', payload);
  assert.strictEqual(piped, payload);

  const child = spawn(process.execPath, ['-e', 'process.stdin.on("data", () => {}); process.stdin.pause(); console.log("paused")'], {
    stdio: ['pipe', 'pipe', 'inherit'],
  });
  let out = '';
  child.stdout.on('data', d => { out += d; });
  const timer = setTimeout(() => {
    child.kill();
    assert.fail('process.stdin.pause() kept the process alive');
  }, 5000);
  child.on('exit', code => {
    clearTimeout(timer);
    child.stdin.destroy();
    assert.strictEqual(code, 0);
    assert.strictEqual(out, 'paused\n');
    fs.rmSync(tmp, { recursive: true, force: true });
    console.log('ok');
  });
} catch (error) {
  fs.rmSync(tmp, { recursive: true, force: true });
  throw error;
}
