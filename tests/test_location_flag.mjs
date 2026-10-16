import assert from 'node:assert';
import http from 'node:http';
import { execFile, spawnSync } from 'node:child_process';
import { promisify } from 'node:util';

const run = (...args) => spawnSync(process.execPath, args, { encoding: 'utf8' });

const fields = run('--location', 'https://example.com:8443/app/page?q=1#top', '-e', `
  console.log(JSON.stringify({ ...location }));
  console.log(new Request('../api?x=1').url);
`);
assert.strictEqual(fields.status, 0, fields.stderr);
const [locationJson, requestUrl] = fields.stdout.trim().split('\n');
assert.deepStrictEqual(JSON.parse(locationJson), {
  href: 'https://example.com:8443/app/page?q=1#top',
  origin: 'https://example.com:8443',
  protocol: 'https:',
  host: 'example.com:8443',
  hostname: 'example.com',
  port: '8443',
  pathname: '/app/page',
  search: '?q=1',
  hash: '#top',
});
assert.strictEqual(requestUrl, 'https://example.com:8443/api?x=1');

const bare = run('--location', 'https://example.com', '-e', 'console.log(JSON.stringify([location.href, location.pathname, location.port, location.search, location.hash]))');
assert.strictEqual(bare.status, 0, bare.stderr);
assert.deepStrictEqual(JSON.parse(bare.stdout), ['https://example.com/', '/', '', '', '']);

for (const bad of ['ftp://x', 'not a url', 'file:///tmp/']) {
  const result = run('--location', bad, '-e', '1');
  assert.strictEqual(result.status, 1, bad);
  assert.match(result.stderr, /--location must be an http: or https: URL/);
}

const none = run('-e', 'console.log(typeof location)');
assert.strictEqual(none.stdout.trim(), 'undefined');

const server = http.createServer((req, res) => res.end(`got ${req.url}`));
await new Promise(resolve => server.listen(0, '127.0.0.1', resolve));
const { port } = server.address();

try {
  const { stdout } = await promisify(execFile)(process.execPath, [
    '--location', `http://127.0.0.1:${port}/base/page`, '-e',
    `fetch('data?z=2').then(r => r.text()).then(t => console.log(t))`,
  ]);
  assert.strictEqual(stdout.trim(), 'got /base/data?z=2');
} finally {
  server.close();
}

console.log('location flag OK');
