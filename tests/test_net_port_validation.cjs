const assert = require('node:assert');
const net = require('node:net');

const badPort = (name) => ({ name: 'RangeError', code: 'ERR_SOCKET_BAD_PORT', message: new RegExp(`^${name} should be >= 0 and < 65536\\.`) });

for (const port of ['', ' ', 'abc', '80.5', '-1', '70000', 80.5, -1, 70000, NaN]) {
  assert.throws(() => net.connect({ port, host: '127.0.0.1' }), badPort('Port'), `connect port ${String(port)}`);
}
assert.throws(() => net.connect({ port: true, host: '127.0.0.1' }), { name: 'TypeError', code: 'ERR_INVALID_ARG_TYPE' });
assert.throws(() => net.connect({ port: null, host: '127.0.0.1' }), { name: 'TypeError', code: 'ERR_INVALID_ARG_TYPE' });
assert.throws(() => net.createServer().listen({ port: 'abc' }), badPort('options.port'));
assert.throws(() => net.createServer().listen(70000), badPort('options.port'));
assert.throws(() => net.createServer().listen({ port: true }), { name: 'TypeError', code: 'ERR_INVALID_ARG_VALUE' });
assert.throws(() => net.createServer().listen({ port: {} }), { name: 'TypeError', code: 'ERR_INVALID_ARG_VALUE' });

const pathSocket = net.connect({ path: '/nonexistent/ant-port-validation.sock', port: 'abc' });
pathSocket.on('error', () => {});

async function connects(options) {
  const server = net.createServer((socket) => socket.end());
  await new Promise((resolve) => server.listen(0, '127.0.0.1', resolve));
  const { port } = server.address();
  try {
    await new Promise((resolve, reject) => {
      const socket = net.connect(options(port));
      socket.on('connect', () => { socket.destroy(); resolve(); });
      socket.on('error', reject);
    });
  } finally {
    server.close();
  }
}

async function listensOn(arg) {
  const server = net.createServer();
  await new Promise((resolve, reject) => {
    server.on('error', reject);
    server.listen(arg, resolve);
  });
  assert.strictEqual(typeof server.address().port, 'number');
  server.close();
}

(async () => {
  await connects((port) => ({ port: String(port), host: '127.0.0.1' }));
  await connects((port) => ({ port: ` ${port} `, host: '127.0.0.1' }));
  await connects((port) => ({ port: `0x${port.toString(16)}`, host: '127.0.0.1' }));
  await listensOn({ port: '0', host: '127.0.0.1' });
  await listensOn('0');
  await listensOn({ port: null, host: '127.0.0.1' });
  console.log('net port validation: ok');
})().catch((err) => {
  console.error(err);
  process.exit(1);
});
