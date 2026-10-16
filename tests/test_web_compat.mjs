import assert from 'node:assert';
import { spawnSync } from 'node:child_process';
import { fileURLToPath } from 'node:url';

const child = fileURLToPath(new URL('./fixtures/web_compat_child.mjs', import.meta.url));
const run = args => spawnSync(process.execPath, [...args, child], { encoding: 'utf8', cwd: fileURLToPath(new URL('.', import.meta.url)) });

const web = run(['--web-compat']);
assert.strictEqual(web.status, 0, web.stderr);
assert.deepStrictEqual(JSON.parse(web.stdout), {
  location: true,
  request: true,
  rootRequest: 'file:///root',
  wsUrl: 'ws://127.0.0.1:9/socket',
  cond: 'browser',
  order: 'import',
  orderRequire: 'require',
  field: 'browser-main',
  map: { server: 'client', plain: 'plain-browser', fs: '{}', util: 'shim', gone: '{}' },
  resolved: 'browser.js',
});

const plain = spawnSync(process.execPath, ['-e', `
  const out = [];
  try { new Request('./api'); out.push('resolved'); } catch (e) { out.push(e.name); }
  out.push(typeof globalThis.location);
  const ws = new WebSocket('https://127.0.0.1:9/a'); ws.onerror = () => {}; out.push(ws.url); ws.close();
  try { new WebSocket('ws://127.0.0.1:9/#f'); } catch (e) { out.push(e.name); }
  try { new WebSocket('ftp://x/'); } catch (e) { out.push(e.name); }
  console.log(JSON.stringify(out));
`], { encoding: 'utf8' });
assert.strictEqual(plain.status, 0, plain.stderr);
assert.deepStrictEqual(JSON.parse(plain.stdout), ['TypeError', 'undefined', 'wss://127.0.0.1:9/a', 'SyntaxError', 'SyntaxError']);

console.log('web compat OK');
