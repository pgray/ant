const assert = require('node:assert');
const http = require('node:http');
const net = require('node:net');

const res = new http.ServerResponse({ method: 'GET', headers: {} });
const invalidChar = { name: 'TypeError', code: 'ERR_INVALID_CHAR' };
const invalidToken = { name: 'TypeError', code: 'ERR_INVALID_HTTP_TOKEN' };

for (const value of ['a\r\nInjected: 1', 'a\nb', 'a\rb', 'a\0b', '€', ['ok', 'b\r\nX: y']]) {
  assert.throws(() => res.setHeader('x-a', value), invalidChar, JSON.stringify(value));
  assert.throws(() => res.appendHeader('x-a', value), invalidChar, JSON.stringify(value));
}
for (const name of ['x a', 'x\r\na', '', 'x:a']) assert.throws(() => res.setHeader(name, 'v'), invalidToken, JSON.stringify(name));
assert.throws(() => res.setHeader('x-u', undefined), { name: 'TypeError', code: 'ERR_HTTP_INVALID_HEADER_VALUE' });
for (const value of ['a\tb', 'café', 5, ['a=1', 'b=2']]) res.setHeader('x-ok', value);

const request = options => http.request({ host: '127.0.0.1', port: 1, ...options }).on('error', () => {}).destroy();
assert.throws(() => request({ path: '/a b' }), { name: 'TypeError', code: 'ERR_UNESCAPED_CHARACTERS' });
assert.throws(() => request({ path: '/a\r\nX: y' }), { name: 'TypeError', code: 'ERR_UNESCAPED_CHARACTERS' });
assert.throws(() => request({ method: 'GE T' }), invalidToken);
assert.throws(() => request({ headers: { 'x-a': 'a\r\nb' } }), invalidChar);
request({ path: '/caf%C3%A9?q=1', headers: { 'x-a': 'v' } });

const server = http.createServer((req, res) => {
  const value = decodeURIComponent(req.url.slice(req.url.indexOf('/', 1) + 1));
  try {
    if (req.url.startsWith('/status/')) res.writeHead(200, value);
    else if (req.url.startsWith('/head/')) res.writeHead(200, { 'x-echo': value });
    else if (req.url.startsWith('/message/')) res.statusMessage = value;
    else res.setHeader('x-echo', value);
    res.end('body');
  } catch (err) {
    res.statusCode = 500;
    res.statusMessage = 'Internal';
    res.removeHeader('x-echo');
    res.end(err.code);
  }
});

function get(port, path) {
  return new Promise((resolve, reject) => {
    const socket = net.connect(port, '127.0.0.1', () => {
      socket.write(`GET ${path} HTTP/1.1\r\nHost: localhost\r\nConnection: close\r\n\r\n`);
    });
    let data = '';
    socket.on('data', chunk => (data += chunk));
    socket.on('close', () => resolve(data));
    socket.on('error', reject);
  });
}

server.listen(0, '127.0.0.1', async () => {
  const { port } = server.address();
  try {
    for (const kind of ['header', 'status', 'head', 'message']) {
      const raw = await get(port, `/${kind}/ok%0D%0AInjected:%20yes`);
      const [head, body] = raw.split('\r\n\r\n');
      assert.match(head, /^HTTP\/1\.1 500 Internal\r\n/, kind);
      assert.doesNotMatch(head, /^Injected/im, kind);
      assert.ok(body.includes('ERR_INVALID_CHAR'), kind);
    }
    assert.match(await get(port, '/header/plain'), /\r\nx-echo: plain\r\n/i);
    console.log('node http header validation test passed');
  } finally {
    server.close();
  }
});
