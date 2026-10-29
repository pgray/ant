const net = require('node:net');
const assert = require('node:assert');

const chunk = Buffer.alloc(16 * 1024, 97);
const total = 8 * 1024 * 1024;
let falses = 0;
let drains = 0;
let callbacks = 0;

const timeout = setTimeout(() => {
  throw new Error('net write backpressure timed out');
}, 5000);

const server = net.createServer(sock => {
  let sent = 0;
  sock.on('drain', () => { drains++; });
  const pump = () => {
    while (sent < total) {
      sent += chunk.length;
      if (!sock.write(chunk, () => { callbacks++; })) {
        falses++;
        return sock.once('drain', pump);
      }
    }
    sock.end();
  };
  pump();
});

server.listen(0, '127.0.0.1', () => {
  const client = net.connect(server.address().port, '127.0.0.1');
  let received = 0;
  let bad = false;
  client.on('data', d => {
    received += d.length;
    for (let i = 0; i < d.length; i += 4096) if (d[i] !== 97) bad = true;
  });
  client.on('end', () => {
    server.close();
    clearTimeout(timeout);
    assert.strictEqual(received, total);
    assert.strictEqual(bad, false);
    assert.strictEqual(callbacks, total / chunk.length);
    assert.ok(falses > 0, 'write() never returned false');
    assert.strictEqual(drains, falses);
    console.log('ok');
  });
});
