const assert = require('node:assert');
const { spawnSync } = require('node:child_process');
const fs = require('node:fs');
const os = require('node:os');
const path = require('node:path');

const ant = path.resolve(process.execPath);
const runtime = path.join(path.dirname(ant), 'ant-runtime');
const tmp = fs.mkdtempSync(path.join(os.tmpdir(), 'ant-cli-preload-'));

function write(name, source) {
  const file = path.join(tmp, name);
  fs.mkdirSync(path.dirname(file), { recursive: true });
  fs.writeFileSync(file, source);
  return file;
}

function run(cmd, args, env = {}) {
  const result = spawnSync(cmd, args, {
    cwd: tmp,
    encoding: 'utf8',
    env: { ...process.env, ANT_OPTIONS: '', ...env },
  });
  if (result.error) throw result.error;
  return result;
}

function lines(result) {
  assert.equal(result.status, 0, result.stderr);
  return result.stdout.trim().split('\n');
}

try {
  const a = write('a.cjs', `(globalThis.order ??= []).push('a'); console.log('a');`);
  const b = write('b.mjs', `(globalThis.order ??= []).push('b'); console.log('b');`);
  const ts = write('typed.ts', `const tag: string = 'ts'; console.log(tag);`);
  const spaced = write('with space/s.cjs', `console.log('spaced');`);
  const entry = write('entry.cjs', `console.log('entry', JSON.stringify(globalThis.order ?? []), JSON.stringify(process.argv.slice(2)));`);
  const exit = write('exit.cjs', `console.log('version:', typeof Ant.version); process.exit(0);`);
  const thrower = write('throw.cjs', `throw new Error('preload boom');`);

  assert.deepEqual(lines(run(ant, ['--preload', a, entry])), ['a', 'entry ["a"] []']);
  assert.deepEqual(lines(run(ant, ['-r', a, `--preload=${b}`, entry])), ['a', 'b', 'entry ["a","b"] []']);
  assert.deepEqual(lines(run(ant, ['--preload', './typed.ts', entry])), ['ts', 'entry [] []']);
  assert.deepEqual(lines(run(ant, [entry, '--preload', a])), ['entry [] ["--preload","' + a + '"]']);
  assert.deepEqual(lines(run(ant, ['-r', a, '-p', 'globalThis.order.join()'])), ['a', 'a']);
  assert.deepEqual(lines(run(ant, ['-r', 'data:text/javascript,console.log("inline")', entry])), ['inline', 'entry [] []']);
  assert.deepEqual(lines(run(ant, [entry], { ANT_OPTIONS: `--preload='data:text/javascript,globalThis.order=["env"]'` })), ['entry ["env"] []']);

  assert.deepEqual(lines(run(ant, [entry], { ANT_OPTIONS: `--preload=${a}` })), ['a', 'entry ["a"] []']);
  assert.deepEqual(lines(run(ant, ['-r', b, entry], { ANT_OPTIONS: `-r ${a}` })), ['a', 'b', 'entry ["a","b"] []']);
  assert.deepEqual(lines(run(ant, [entry], { ANT_OPTIONS: `--preload "${spaced}"` })), ['spaced', 'entry [] []']);
  assert.deepEqual(lines(run(ant, [entry], { ANT_OPTIONS: `--preload=${exit}` })), ['version: string']);

  let r = run(ant, ['--preload', './missing.cjs', entry]);
  assert.notEqual(r.status, 0);
  assert.match(r.stderr, /Preload not found: "\.\/missing\.cjs"/);
  assert.equal(r.stdout, '');

  r = run(ant, ['--preload', thrower, entry]);
  assert.notEqual(r.status, 0);
  assert.match(r.stderr, /preload boom/);
  assert.equal(r.stdout, '');

  if (fs.existsSync(runtime)) {
    const app = path.join(tmp, 'app');
    r = run(ant, ['compile', '--runtime', runtime, '-o', app, entry]);
    assert.equal(r.status, 0, `compile failed: ${r.stderr}`);

    assert.deepEqual(lines(run(app, ['x'])), ['entry [] ["x"]']);
    assert.deepEqual(lines(run(app, ['x'], { ANT_OPTIONS: `--preload=${a} --jitless -r ${b}` })), ['a', 'b', 'entry ["a","b"] ["x"]']);
    assert.deepEqual(lines(run(app, [], { ANT_OPTIONS: `--preload=${exit}` })), ['version: string']);
  } else console.log(`skip compiled: ${runtime} not built`);
} finally {
  fs.rmSync(tmp, { recursive: true, force: true });
}

console.log('cli preload runs modules before the entry');
