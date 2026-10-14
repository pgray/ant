const assert = require('node:assert');

const server = Ant.serve({
  hostname: '127.0.0.1',
  port: 0,
  fetch() {
    const enc = new TextEncoder();
    const body = new ReadableStream({ start(c) { c.enqueue(enc.encode('data: one\n\n')); } });
    return new Response(body, { headers: { 'content-type': 'text/event-stream' } });
  },
});

const es = new EventSource(`http://127.0.0.1:${server.port}/`);
const order = [];

assert.strictEqual(es.onmessage, null);
assert.deepStrictEqual(Object.keys(es).filter(k => k.startsWith('on')), []);

es.addEventListener('message', () => order.push('first'));
es.onmessage = () => order.push('old');
es.addEventListener('message', () => order.push('last'));
es.onmessage = event => order.push(`on:${event.data}:${event.target === es}`);

const timer = setTimeout(() => {
  console.error('timed out', order);
  process.exit(1);
}, 3000);

es.addEventListener('message', () => {
  es.close();
  clearTimeout(timer);
  assert.deepStrictEqual(order, ['first', 'on:one:true', 'last']);
  server.stop?.();
  console.log('eventsource handlers OK');
});
