const assert = require('node:assert');
const http = require('node:http');
const net = require('node:net');

function probe(port, head) {
  return new Promise(resolve => {
    const socket = net.connect(port, '127.0.0.1', () => socket.write(head));
    let out = '';
    socket.setEncoding('latin1');
    socket.on('data', d => {
      out += d;
      if (out.includes('\r\n\r\n')) socket.destroy();
    });
    socket.on('error', () => {});
    socket.on('close', () => resolve(out.split('\r\n')[0]));
  });
}

function manyHeaders(count) {
  let head = 'GET / HTTP/1.1\r\nHost: x\r\n';
  for (let i = 0; i < count; i++) head += `h${i}: v\r\n`;
  return head + '\r\n';
}

async function check(server, label) {
  let reached = 0;
  server.on('request', (req, res) => {
    reached++;
    res.end('ok');
  });
  await new Promise(resolve => server.listen(0, '127.0.0.1', resolve));
  const { port } = server.address();

  assert.match(await probe(port, 'GET / HTTP/1.1\r\nHost: x\r\nConnection: close\r\n\r\n'), / 200 /, label);
  assert.match(await probe(port, manyHeaders(100000)), / 431 /, `${label}: many headers`);
  assert.match(await probe(port, `GET / HTTP/1.1\r\nHost: x\r\nX-Big: ${'a'.repeat(64 * 1024)}\r\n\r\n`), / 431 /, `${label}: one big header`);
  assert.match(await probe(port, `GET /${'p'.repeat(32 * 1024)} HTTP/1.1\r\nHost: x\r\n\r\n`), / 431 /, `${label}: long target`);
  assert.strictEqual(reached, 1, label);

  await new Promise(resolve => server.close(resolve));
}

(async () => {
  await check(http.createServer(), 'default limit');

  const small = http.createServer({ maxHeaderSize: 256 });
  let reached = 0;
  small.on('request', (req, res) => {
    reached++;
    res.end('ok');
  });
  await new Promise(resolve => small.listen(0, '127.0.0.1', resolve));
  const { port } = small.address();
  assert.match(await probe(port, `GET / HTTP/1.1\r\nHost: x\r\nX-Pad: ${'a'.repeat(512)}\r\nConnection: close\r\n\r\n`), / 431 /);
  assert.match(await probe(port, 'GET / HTTP/1.1\r\nHost: x\r\nConnection: close\r\n\r\n'), / 200 /);
  assert.strictEqual(reached, 1);
  await new Promise(resolve => small.close(resolve));

  console.log('ok');
})();
