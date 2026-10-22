const assert = require('node:assert');
const { spawn } = require('node:child_process');
const http = require('node:http');

const serverSource = `
const net = require('node:net');
const server = net.createServer(sock => {
  let requests = 0;
  let buf = '';
  sock.on('data', d => {
    buf += d;
    while (buf.includes('\\r\\n\\r\\n')) {
      buf = buf.slice(buf.indexOf('\\r\\n\\r\\n') + 4);
      requests++;
      if (requests === 1) {
        sock.write('HTTP/1.1 200 OK\\r\\nContent-Length: 5\\r\\nConnection: keep-alive\\r\\n\\r\\nfirst');
        setTimeout(() => sock.write('HTTP/1.1 200 OK\\r\\nContent-Length: 8\\r\\nConnection: keep-alive\\r\\n\\r\\nINJECTED'), 20);
      } else {
        sock.write('HTTP/1.1 200 OK\\r\\nContent-Length: 4\\r\\nConnection: keep-alive\\r\\n\\r\\nreal');
      }
    }
  });
  sock.on('error', () => {});
});
server.listen(0, '127.0.0.1', () => console.log(server.address().port));
`;

function spin(ms) {
  const end = Date.now() + ms;
  while (Date.now() < end) {}
}

function viaHttp(port, agent) {
  return new Promise((resolve, reject) => {
    http.get({ host: '127.0.0.1', port, path: '/', agent }, res => {
      let body = '';
      res.setEncoding('utf8');
      res.on('data', d => { body += d; });
      res.on('end', () => resolve(body));
    }).on('error', reject);
  });
}

async function poisonedReuse(get) {
  const first = await get();
  await new Promise(r => setImmediate(r));
  spin(150);
  const second = await get();
  return [first, second];
}

const child = spawn(process.execPath, ['-e', serverSource], { stdio: ['ignore', 'pipe', 'inherit'] });
child.stdout.setEncoding('utf8');
child.stdout.once('data', async data => {
  const port = Number(data.trim());
  try {
    const agent = new http.Agent({ keepAlive: true, maxSockets: 1 });
    const viaAgent = await poisonedReuse(() => viaHttp(port, agent));
    agent.destroy();
    assert.strictEqual(viaAgent[0], 'first');
    assert.notStrictEqual(viaAgent[1], 'INJECTED');

    const viaFetch = await poisonedReuse(() => fetch(`http://127.0.0.1:${port}/`).then(r => r.text()));
    assert.ok(!viaFetch.includes('INJECTED'), JSON.stringify(viaFetch));
    console.log('ok');
  } finally {
    child.kill();
  }
});
