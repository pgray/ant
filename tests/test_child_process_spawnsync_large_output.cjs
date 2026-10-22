const assert = require('node:assert');
const { spawnSync } = require('node:child_process');

const size = 1024 * 1024;
const script = `
  let got = 0;
  process.stdin.on('data', d => { got += d.length; });
  process.stdin.on('end', () => {
    process.stderr.write('e'.repeat(${size}));
    process.stdout.write('o'.repeat(${size}) + ':' + got);
  });
`;

const r = spawnSync(process.execPath, ['-e', script], {
  input: 'i'.repeat(size),
  encoding: 'utf8',
  maxBuffer: 4 * size,
});

assert.strictEqual(r.status, 0, r.error && r.error.message);
assert.strictEqual(r.stderr.length, size);
assert.strictEqual(r.stdout, 'o'.repeat(size) + ':' + size);

console.log('ok');
