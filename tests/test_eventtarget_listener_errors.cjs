const assert = require('node:assert');

const log = [];
let finished = false;
process.once('uncaughtException', error => log.push('uncaught:' + error.message));
process.on('exit', () => {
  if (!finished) process.exitCode = 1;
  else assert.ok(Date.now() - started < 30000, 'AbortSignal.timeout kept the process alive');
});

const controller = new AbortController();
controller.signal.addEventListener('abort', () => { log.push('first'); throw new Error('boom'); });
controller.signal.addEventListener('abort', { tag: 'obj', handleEvent(event) { log.push(`handleEvent:${event.type}:${this.tag}`); } });
controller.signal.addEventListener('abort', () => log.push('last'));
controller.abort();
log.push('abort returned');

const target = new EventTarget();
let calls = 0;
const handler = { handleEvent() { calls++; } };
target.addEventListener('x', handler);
target.addEventListener('x', handler);
target.dispatchEvent(new Event('x'));
target.removeEventListener('x', handler);
target.dispatchEvent(new Event('x'));
assert.strictEqual(calls, 1);

var started = Date.now();
AbortSignal.timeout(60000);

setTimeout(() => {
  assert.deepStrictEqual(log, ['first', 'handleEvent:abort:obj', 'last', 'abort returned', 'uncaught:boom']);
  finished = true;
  console.log('eventtarget listener errors OK');
}, 30);
