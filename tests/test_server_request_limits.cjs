const assert = require('node:assert');
const crypto = require('node:crypto');
const net = require('node:net');

function probe(port, data, waitMs = 1500) {
  return new Promise(resolve => {
    const socket = net.connect(port, '127.0.0.1', () => socket.write(data));
    let out = '';
    const timer = setTimeout(() => socket.destroy(), waitMs);
    socket.setEncoding('latin1');
    socket.on('data', d => {
      out += d;
      if (out.includes('\r\n\r\n')) socket.destroy();
    });
    socket.on('error', () => {});
    socket.on('close', () => {
      clearTimeout(timer);
      resolve(out.split('\r\n')[0]);
    });
  });
}

function trickle(port, intervalMs, limitMs) {
  return new Promise(resolve => {
    const started = Date.now();
    const socket = net.connect(port, '127.0.0.1');
    const head = 'GET / HTTP/1.1\r\nHost: x\r\nX-Slow: ' + 'a'.repeat(1000);
    let i = 0;
    const timer = setInterval(() => {
      if (i < head.length) socket.write(head[i++]);
    }, intervalMs);
    const stop = setTimeout(() => socket.destroy(), limitMs);
    socket.on('error', () => {});
    socket.on('close', () => {
      clearInterval(timer);
      clearTimeout(stop);
      resolve(Date.now() - started);
    });
  });
}

function websocketOversizeClose(port) {
  return new Promise((resolve, reject) => {
    let buffer = Buffer.alloc(0);
    let upgraded = false;
    const timer = setTimeout(() => {
      socket.destroy();
      resolve(null);
    }, 2000);
    const socket = net.connect(port, '127.0.0.1', () => {
      socket.write(
        'GET /ws HTTP/1.1\r\nHost: 127.0.0.1\r\nConnection: Upgrade\r\nUpgrade: websocket\r\n' +
        `Sec-WebSocket-Version: 13\r\nSec-WebSocket-Key: ${crypto.randomBytes(16).toString('base64')}\r\n\r\n`
      );
    });
    socket.on('data', chunk => {
      buffer = Buffer.concat([buffer, chunk]);
      if (!upgraded) {
        const end = buffer.indexOf('\r\n\r\n');
        if (end === -1) return;
        upgraded = true;
        buffer = buffer.subarray(end + 4);
        socket.write(Buffer.from([0x82, 0xff, 0, 0, 0, 0, 0, 0x10, 0, 0, 1, 2, 3, 4]));
      }
      if (buffer.length >= 4 && (buffer[0] & 0x0f) === 0x8) {
        clearTimeout(timer);
        socket.destroy();
        resolve(buffer.readUInt16BE(2));
      }
    });
    socket.on('error', reject);
  });
}

let reached = 0;
const server = Ant.serve({
  hostname: '127.0.0.1',
  port: 0,
  maxRequestBodySize: 1024,
  requestTimeout: 1,
  websocket: { maxPayloadLength: 1024 },
  fetch(request, ctx) {
    if (new URL(request.url).pathname === '/ws') return ctx.upgradeWebSocket(request).response;
    reached++;
    return new Response('ok');
  },
});

(async () => {
  const port = server.port;

  assert.match(await probe(port, 'POST / HTTP/1.1\r\nHost: x\r\nContent-Length: 100000000000\r\n\r\n'), / 413 /);
  const chunk = 'a'.repeat(800);
  const chunked = `POST / HTTP/1.1\r\nHost: x\r\nTransfer-Encoding: chunked\r\n\r\n320\r\n${chunk}\r\n320\r\n${chunk}\r\n0\r\n\r\n`;
  assert.match(await probe(port, chunked), / 413 /);
  assert.match(await probe(port, `POST / HTTP/1.1\r\nHost: x\r\nConnection: close\r\nContent-Length: 4\r\n\r\nabcd`), / 200 /);
  assert.strictEqual(reached, 1);

  const elapsed = await trickle(port, 200, 6000);
  assert.ok(elapsed < 3000, `slow request held the connection for ${elapsed}ms`);

  assert.strictEqual(await websocketOversizeClose(port), 1009);

  await server.stop();
  console.log('ok');
})();
