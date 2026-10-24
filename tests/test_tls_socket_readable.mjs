import assert from 'node:assert';
import fs from 'node:fs';
import tls from 'node:tls';

const fixture = (name) => fs.readFileSync(new URL(`./fixtures/tls/${name}`, import.meta.url));
const ca = fixture('ca-cert.pem');
const serverOptions = { key: fixture('server-key.pem'), cert: fixture('server-cert.pem') };
const connect = (port, extra = {}) => tls.connect({ host: '127.0.0.1', servername: 'localhost', ca, port, ...extra });
const once = (emitter, event) => new Promise((resolve) => emitter.once(event, resolve));
const tick = () => new Promise((resolve) => setTimeout(resolve, 30));

async function withServer(onConnection, fn) {
  const server = tls.createServer(serverOptions, onConnection);
  server.listen(0, '127.0.0.1');
  await once(server, 'listening');
  try { await fn(server.address().port); } finally { server.close(); }
}

await withServer((s) => { s.write('abcdef'); setTimeout(() => s.end('ghij'), 50); }, async (port) => {
  const sock = connect(port);
  await once(sock, 'readable');
  assert.strictEqual(String(sock.read(2)), 'ab');
  assert.strictEqual(String(sock.read()), 'cdef');
  await tick(); await tick(); await tick();
  assert.strictEqual(String(sock.read(100)), 'ghij');
  sock.destroy();
});

await withServer((s) => {
  let request = '';
  s.on('data', (d) => (request += d));
  s.on('end', () => s.end(`response to ${request}`));
}, async (port) => {
  const sock = connect(port);
  const got = [];
  const events = [];
  sock.on('data', (d) => got.push(String(d)));
  sock.on('end', () => events.push('end'));
  sock.on('close', () => events.push('close'));
  sock.on('secureConnect', () => sock.end('ping'));
  await once(sock, 'close');
  assert.strictEqual(got.join(''), 'response to ping');
  assert.deepStrictEqual(events, ['end', 'close']);
});

await withServer((s) => { s.on('end', () => s.end('bye')); }, async (port) => {
  const sock = connect(port, { allowHalfOpen: true });
  const got = [];
  sock.on('data', (d) => got.push(String(d)));
  sock.on('secureConnect', () => sock.end());
  await once(sock, 'close');
  assert.strictEqual(got.join(''), 'bye');
});

await withServer((s) => s.end('héllo'), async (port) => {
  const sock = connect(port);
  sock.setEncoding('utf8');
  const parts = [];
  sock.on('data', (d) => parts.push(d));
  await once(sock, 'end');
  assert.deepStrictEqual(parts, ['héllo']);
  sock.destroy();
});

await withServer(() => {}, async (port) => {
  const sock = connect(String(port));
  await once(sock, 'secureConnect');
  sock.destroy();
});

console.log('tls socket readable: ok');
