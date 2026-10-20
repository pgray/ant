const assert = require('node:assert');
const { spawnSync } = require('node:child_process');
const fs = require('node:fs');
const os = require('node:os');
const path = require('node:path');

const ant = path.resolve(process.execPath);
const runtime = path.join(path.dirname(ant), 'ant-runtime');

if (!fs.existsSync(runtime)) {
  console.log(`skip: ${runtime} not built (ninja -C build ant-runtime)`);
  process.exit(0);
}

const files = {
  'package.json': '{"type":"commonjs"}',
  'index.cjs': `
    const path = require('node:path');
    const { join } = require('node:path');
    const { Worker: NodeWorker } = require('node:worker_threads');

    console.log('joined:', require(path.join(__dirname, 'lib', 'a.js')));
    console.log('ternary:', require(process.env.PICK === 'c' ? './lib/c.js' : './lib/b.js'));
    console.log('locale:', require(\`./locales/\${process.env.LOCALE || 'en'}.json\`).hello);
    const name = process.env.PLUGIN;
    console.log('plugin:', require('./plugins/' + name));
    let base = './lib';
    console.log('let:', require(base + '/d.js'));

    function shadowed(require) { return require('./not-a-module.js'); }
    if (process.platform === 'nonexistent') require('./pruned-away.js');
    console.log('shadowed:', typeof shadowed);

    const nw = new NodeWorker(join(__dirname, 'workers', 'node.cjs'));
    nw.on('message', m => {
      console.log('node worker:', m);
      const ww = new Worker('./workers/web.cjs');
      ww.onmessage = e => { console.log('web worker:', e.data); ww.terminate(); };
    });
  `,
  'lib/a.js': 'module.exports = "a";',
  'lib/b.js': 'module.exports = "b";',
  'lib/c.js': 'module.exports = "c";',
  'lib/d.js': 'module.exports = "d";',
  'locales/en.json': '{"hello":"hello"}',
  'locales/fr.json': '{"hello":"bonjour"}',
  'plugins/alpha.js': 'module.exports = "alpha";',
  'plugins/beta.js': 'module.exports = "beta";',
  'workers/node.cjs': 'require("node:worker_threads").parentPort.postMessage("ok")',
  'workers/web.cjs': 'postMessage("ok")',
};

const tmp = fs.mkdtempSync(path.join(os.tmpdir(), 'ant-compile-infer-'));
try {
  const app = path.join(tmp, 'app');
  for (const [name, text] of Object.entries(files)) {
    fs.mkdirSync(path.dirname(path.join(app, name)), { recursive: true });
    fs.writeFileSync(path.join(app, name), text);
  }

  const out = path.join(tmp, 'compiled');
  let r = spawnSync(ant, ['compile', '--runtime', runtime, '-o', out, path.join(app, 'index.cjs')], { encoding: 'utf8' });
  assert.equal(r.status, 0, `compile failed: ${r.stderr}`);
  assert.doesNotMatch(r.stderr, /non-constant/, r.stderr);
  fs.rmSync(app, { recursive: true, force: true });

  const run = env => spawnSync(out, [], { encoding: 'utf8', cwd: os.homedir(), env: { ...process.env, ...env } });

  r = run({ PLUGIN: 'alpha' });
  assert.equal(r.status, 0, r.stderr);
  for (const line of ['joined: a', 'ternary: b', 'locale: hello', 'plugin: alpha', 'let: d', 'shadowed: function', 'node worker: ok', 'web worker: ok'])
    assert.ok(r.stdout.includes(line), `${line}\n${r.stdout}`);

  r = run({ PICK: 'c', LOCALE: 'fr', PLUGIN: 'beta' });
  assert.equal(r.status, 0, r.stderr);
  for (const line of ['ternary: c', 'locale: bonjour', 'plugin: beta'])
    assert.ok(r.stdout.includes(line), `${line}\n${r.stdout}`);
} finally {
  fs.rmSync(tmp, { recursive: true, force: true });
}

console.log('compile inference: ok');
