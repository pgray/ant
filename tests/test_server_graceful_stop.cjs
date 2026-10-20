const assert = require('node:assert/strict');

const sleep = ms => new Promise(r => setTimeout(r, ms));

async function idleKeepAlive() {
  const server = Ant.serve({ hostname: '127.0.0.1', port: 0, fetch: () => new Response('ok') });
  const res = await fetch(`${server.url}/`);
  assert.equal(await res.text(), 'ok');
  assert.equal(res.headers.get('connection'), 'keep-alive');

  const started = Date.now();
  await server.stop();
  const took = Date.now() - started;
  assert.ok(took < 1000, `stop() waited ${took}ms on an idle keep-alive connection`);
  await assert.rejects(fetch(`${server.url}/`));
}

async function inFlightFinishes() {
  let release;
  const gate = new Promise(r => { release = r; });
  const server = Ant.serve({
    hostname: '127.0.0.1',
    port: 0,
    async fetch() {
      await gate;
      return new Response('x'.repeat(64 * 1024));
    },
  });

  const pending = fetch(`${server.url}/slow`);
  await sleep(50);

  let stopped = false;
  const stopping = server.stop().then(() => { stopped = true; });
  await sleep(50);
  assert.equal(stopped, false);

  release();
  const res = await pending;
  assert.equal(res.headers.get('connection'), 'close');
  assert.equal((await res.text()).length, 64 * 1024);

  await stopping;
  assert.equal(stopped, true);
}

async function forceStopCutsInFlight() {
  const server = Ant.serve({
    hostname: '127.0.0.1',
    port: 0,
    fetch: () => new Promise(() => {}),
  });

  const pending = fetch(`${server.url}/hang`).then(() => 'resolved', () => 'rejected');
  await sleep(50);
  await server.stop(true);
  assert.equal(await pending, 'rejected');
}

(async () => {
  await idleKeepAlive();
  await inFlightFinishes();
  await forceStopCutsInFlight();
  console.log('ok');
})();
