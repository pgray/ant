const assert = require('node:assert');
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

let reached = 0;
const server = Ant.serve({
  hostname: '127.0.0.1',
  port: 0,
  fetch: () => {
    reached++;
    return new Response('ok');
  },
});

(async () => {
  const port = server.port;
  let many = 'GET / HTTP/1.1\r\nHost: x\r\n';
  for (let i = 0; i < 100000; i++) many += `h${i}: v\r\n`;

  assert.match(await probe(port, 'GET / HTTP/1.1\r\nHost: x\r\nConnection: close\r\n\r\n'), / 200 /);
  assert.match(await probe(port, many + '\r\n'), / 431 /);
  assert.match(await probe(port, `GET / HTTP/1.1\r\nHost: x\r\nX-Big: ${'a'.repeat(64 * 1024)}\r\n\r\n`), / 431 /);
  assert.match(await probe(port, `GET /${'p'.repeat(32 * 1024)} HTTP/1.1\r\nHost: x\r\n\r\n`), / 431 /);
  assert.strictEqual(reached, 1);

  await server.stop();
  console.log('ok');
})();
