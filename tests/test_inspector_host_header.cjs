const assert = require('node:assert');
const { spawn } = require('node:child_process');
const net = require('node:net');

async function reservePort() {
  const server = net.createServer();
  await new Promise(resolve => server.listen(0, '127.0.0.1', resolve));
  const { port } = server.address();
  await new Promise(resolve => server.close(resolve));
  return port;
}

function request(port, path, host, extra = '') {
  return new Promise((resolve, reject) => {
    const socket = net.connect(port, '127.0.0.1', () => {
      const hostLine = host === null ? '' : `Host: ${host}\r\n`;
      socket.write(`GET ${path} HTTP/1.1\r\n${hostLine}${extra}\r\n`);
    });
    let data = '';
    socket.on('data', chunk => {
      data += chunk;
      if (data.includes('\r\n\r\n')) socket.destroy();
    });
    socket.on('close', () => resolve(Number(/^HTTP\/1\.1 (\d{3})/.exec(data)?.[1] ?? 0)));
    socket.on('error', reject);
  });
}

async function main() {
  const port = await reservePort();
  const child = spawn(process.execPath, [`--inspect=127.0.0.1:${port}`, '-e', 'setInterval(() => {}, 1000)'], {
    stdio: ['ignore', 'ignore', 'pipe'],
  });
  let stderr = '';
  child.stderr.on('data', chunk => (stderr += chunk));

  try {
    let targets;
    const deadline = Date.now() + 3000;
    while (!targets && Date.now() < deadline && child.exitCode === null) {
      try {
        targets = await (await fetch(`http://127.0.0.1:${port}/json/list`)).json();
      } catch {
        await new Promise(resolve => setTimeout(resolve, 10));
      }
    }
    assert.ok(targets, stderr);
    const wsPath = new URL(targets[0].webSocketDebuggerUrl).pathname;
    const upgrade = 'Upgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Version: 13\r\nSec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\n';

    const allowed = [null, 'localhost', `localhost:${port}`, 'LOCALHOST', `127.0.0.1:${port}`, '127.0.0.1', `[::1]:${port}`, '[::1]', '10.0.0.5'];
    const rejected = [
      'localhost.attacker.invalid',
      `localhost.attacker.invalid:${port}`,
      '127.0.0.1.attacker.invalid',
      `127.0.0.1.nip.io:${port}`,
      '[::1].attacker.invalid',
      'localhostx',
      'attacker.invalid',
      `localhost:${port}x`,
      'localhost:',
      '[::1',
      '',
    ];

    for (const host of allowed) assert.strictEqual(await request(port, '/json/list', host), 200, `json/list Host: ${host}`);
    for (const host of rejected) {
      assert.strictEqual(await request(port, '/json/list', host), 403, `json/list Host: ${host}`);
      assert.strictEqual(await request(port, wsPath, host, upgrade), 403, `upgrade Host: ${host}`);
    }
    assert.strictEqual(await request(port, wsPath, `localhost:${port}`, upgrade), 101);
  } finally {
    child.kill();
  }
  console.log('inspector host header test passed');
}

main().catch(error => {
  console.error(error);
  process.exit(1);
});
