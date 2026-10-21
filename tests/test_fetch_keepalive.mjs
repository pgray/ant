import assert from 'node:assert/strict';
import net from 'node:net';
import { spawn } from 'node:child_process';
import { Worker } from 'node:worker_threads';

let connections = 0;
const closed = [];
const server = net.createServer(socket => {
  const id = ++connections;
  let buffer = '';
  let served = 0;
  socket.on('close', () => closed.push(id));
  socket.on('error', () => {});
  socket.on('data', chunk => {
    buffer += chunk.toString('latin1');
    let end;
    while ((end = buffer.indexOf('\r\n\r\n')) >= 0) {
      const head = buffer.slice(0, end);
      buffer = buffer.slice(end + 4);
      const length = Number(/content-length:\s*(\d+)/i.exec(head)?.[1] ?? 0);
      buffer = buffer.slice(length);
      const path = head.split(' ')[1];
      served++;
      respond(socket, path, served, id);
    }
  });
});

function reply(socket, status, body, extra = '') {
  socket.write(`HTTP/1.1 ${status}\r\nContent-Length: ${Buffer.byteLength(body)}\r\nConnection: keep-alive\r\n${extra}\r\n${body}`);
}

function respond(socket, path, served, id) {
  if (path === '/close') {
    socket.end(`HTTP/1.1 200 OK\r\nContent-Length: 5\r\nConnection: close\r\n\r\nclose`);
  } else if (path === '/stale' && served > 1) {
    socket.destroy();
  } else if (path === '/drop') {
    socket.destroy();
  } else if (path === '/hang') {
    socket.write('HTTP/1.1 200 OK\r\nContent-Length: 100\r\n\r\npartial');
  } else if (path === '/redirect') {
    reply(socket, '302 Found', '', 'Location: /target\r\n');
  } else {
    reply(socket, '200 OK', `${path} via ${id}`);
  }
}

await new Promise(r => server.listen(0, '127.0.0.1', r));
const base = `http://127.0.0.1:${server.address().port}`;
const text = async path => (await fetch(base + path)).text();

{
  const before = connections;
  for (let i = 0; i < 20; i++) assert.match(await text('/seq'), /^\/seq via \d+$/);
  assert.ok(connections - before <= 2, `sequential fetches opened ${connections - before} connections`);
}

{
  const before = connections;
  const bodies = await Promise.all(Array.from({ length: 10 }, () => text('/par')));
  assert.ok(bodies.every(b => b.startsWith('/par via')));
  assert.ok(connections - before <= 10, `parallel fetches opened ${connections - before} connections`);
  const after = connections;
  for (let i = 0; i < 10; i++) await text('/again');
  assert.equal(connections, after, 'idle pooled connections were not reused');
}

{
  assert.equal(await text('/close'), 'close');
  assert.match(await text('/after-close'), /^\/after-close via \d+$/);
}

{
  assert.match(await text('/stale'), /^\/stale via \d+$/);
  assert.match(await text('/stale'), /^\/stale via \d+$/, 'stale reused connection was not retried');
}

{
  const controller = new AbortController();
  const response = await fetch(base + '/hang', { signal: controller.signal });
  const reading = response.text();
  const hungId = connections;
  controller.abort();
  await assert.rejects(reading, { name: 'AbortError' });
  assert.match(await text('/after-abort'), /^\/after-abort via \d+$/);
  await new Promise(r => setTimeout(r, 50));
  assert.ok(closed.includes(hungId), 'aborted connection was not closed');
}

await assert.rejects(fetch(base + '/drop'), TypeError);
assert.match(await text('/after-drop'), /^\/after-drop via \d+$/);

{
  const response = await fetch(base + '/redirect');
  assert.equal(response.redirected, true);
  assert.match(await response.text(), /^\/target via \d+$/);
}

{
  const worker = new Worker(`
    const { parentPort, workerData } = require('worker_threads');
    (async () => {
      const bodies = [];
      for (let i = 0; i < 3; i++) bodies.push(await (await fetch(workerData + '/worker')).text());
      parentPort.postMessage(bodies);
    })();
  `, { eval: true, workerData: base });
  const bodies = await new Promise((resolve, reject) => { worker.once('message', resolve); worker.once('error', reject); });
  assert.equal(bodies.length, 3);
  assert.ok(bodies.every(b => b.startsWith('/worker via')));
  await new Promise(r => worker.once('exit', r));
}

{
  const start = Date.now();
  const child = spawn(process.execPath, ['-e', `fetch(${JSON.stringify(base + '/exit')}).then(r => r.text()).then(t => console.log(t))`], { stdio: ['ignore', 'pipe', 'inherit'] });
  let out = '';
  child.stdout.on('data', d => { out += d; });
  const code = await new Promise(r => child.on('exit', r));
  assert.equal(code, 0);
  assert.match(out, /^\/exit via \d+/);
  assert.ok(Date.now() - start < 2000, `process with an idle pooled connection took ${Date.now() - start}ms to exit`);
}

server.close();
console.log('fetch keepalive: ok');
