const assert = require('node:assert');
const { spawn, spawnSync } = require('node:child_process');
const crypto = require('node:crypto');
const fs = require('node:fs');
const http = require('node:http');
const os = require('node:os');
const path = require('node:path');

if (process.platform === 'win32') {
  console.log('skipping package lifecycle test on win32');
  process.exit(0);
}

const ant = path.resolve(process.execPath);
const tmp = fs.realpathSync(fs.mkdtempSync(path.join(os.tmpdir(), 'ant-pkg-production-dev-')));
const home = path.join(tmp, 'home');
const outPath = path.join(tmp, 'lifecycle.log');
const project = path.join(tmp, 'project');
const files = new Map();
const packuments = new Map();

function record(tag) {
  return { postinstall: `printf "${tag}," >> "$ANT_TEST_LIFECYCLE_OUT"` };
}

function tarball(name, manifest) {
  const dir = path.join(tmp, 'src', name, 'package');
  fs.mkdirSync(dir, { recursive: true });
  fs.writeFileSync(path.join(dir, 'package.json'), JSON.stringify(manifest));
  const file = path.join(tmp, 'src', name, 'package.tgz');
  assert.strictEqual(spawnSync('tar', ['-czf', file, '-C', path.dirname(dir), 'package']).status, 0);
  return fs.readFileSync(file);
}

function publish(base, manifest) {
  const bytes = tarball(`${manifest.name}-${manifest.version}`, manifest);
  const url = `/${manifest.name}/-/${manifest.name}-${manifest.version}.tgz`;
  files.set(url, bytes);
  const integrity = `sha512-${crypto.createHash('sha512').update(bytes).digest('base64')}`;
  const doc = packuments.get(manifest.name) ?? { name: manifest.name, versions: {}, 'dist-tags': {}, time: {} };
  doc.versions[manifest.version] = { ...manifest, dist: { tarball: `${base}${url}`, integrity } };
  doc['dist-tags'].latest = manifest.version;
  doc.time[manifest.version] = '2020-01-01T00:00:00.000Z';
  packuments.set(manifest.name, doc);
}

function antIn(dir, ...args) {
  const env = { ...process.env, HOME: home, NO_COLOR: '1', ANT_TEST_LIFECYCLE_OUT: outPath };
  for (const key of ['XDG_DATA_HOME', 'XDG_CACHE_HOME', 'FORCE_COLOR', 'UPM_DEFAULT_REGISTRY', 'UPM_FALLBACK_REGISTRY', 'UPM_STORE']) delete env[key];
  return new Promise((resolve, reject) => {
    const child = spawn(ant, args, { cwd: dir, env });
    let stdout = '';
    let stderr = '';
    child.stdout.on('data', chunk => (stdout += chunk));
    child.stderr.on('data', chunk => (stderr += chunk));
    child.on('error', reject);
    child.on('close', status => resolve({ status, stdout, stderr }));
  });
}

function ok(result, what) {
  assert.strictEqual(result.status, 0, `${what} failed\nstdout:\n${result.stdout}\nstderr:\n${result.stderr}`);
  return result;
}

const ran = () => (fs.existsSync(outPath) ? fs.readFileSync(outPath, 'utf8') : '');

const server = http.createServer((req, res) => {
  const url = decodeURIComponent(req.url.split('?')[0]);
  if (files.has(url)) {
    res.writeHead(200, { 'content-type': 'application/octet-stream' });
    return res.end(files.get(url));
  }
  const doc = packuments.get(url.slice(1));
  if (doc) {
    res.writeHead(200, { 'content-type': 'application/json' });
    return res.end(JSON.stringify(doc));
  }
  res.writeHead(404, { 'content-type': 'application/json' });
  res.end('{"error":"not found"}');
});

async function main() {
  await new Promise(resolve => server.listen(0, '127.0.0.1', resolve));
  const base = `http://127.0.0.1:${server.address().port}`;

  publish(base, { name: 'prodpkg', version: '1.0.0', scripts: record('prodpkg') });
  publish(base, { name: 'devtool', version: '1.0.0', scripts: record('devtool') });

  fs.mkdirSync(home);
  fs.mkdirSync(project);
  fs.writeFileSync(path.join(project, '.npmrc'), `registry=${base}/\n`);
  fs.writeFileSync(path.join(project, 'package.json'), JSON.stringify({
    name: 'production-dev-app',
    dependencies: { prodpkg: '1.0.0' },
    devDependencies: { devtool: '1.0.0' },
    trustedDependencies: ['prodpkg', 'devtool'],
  }));

  ok(await antIn(project, 'install', '--ignore-scripts'), 'install without scripts');
  assert.strictEqual(ran(), '');
  assert.ok(fs.readdirSync(path.join(project, 'node_modules', '.ant')).some(key => key.startsWith('devtool@')));

  ok(await antIn(project, 'install', '--production'), 'production install');
  assert.ok(fs.readdirSync(path.join(project, 'node_modules', '.ant')).some(key => key.startsWith('devtool@')));
  assert.strictEqual(ran(), 'prodpkg,');

  const listed = ok(await antIn(project, 'trust'), 'trust listing');
  assert.doesNotMatch(listed.stdout + listed.stderr, /devtool/);
  assert.strictEqual(ran(), 'prodpkg,');

  ok(await antIn(project, 'install'), 'full install');
  assert.strictEqual(ran(), 'prodpkg,devtool,');

  console.log('ok');
}

main()
  .catch(error => {
    process.exitCode = 1;
    console.error(error);
  })
  .finally(() => {
    server.close();
    fs.rmSync(tmp, { recursive: true, force: true });
  });
