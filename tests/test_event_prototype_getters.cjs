const assert = require('node:assert');
const { inspect } = require('node:util');

const accessor = (proto, key) => {
  const d = Object.getOwnPropertyDescriptor(proto, key);
  return d && `${d.get ? 'g' : ''}${d.set ? 's' : ''}${d.enumerable ? 'e' : '-'}${d.configurable ? 'c' : '-'}`;
};

const event = new Event('x', { bubbles: true, cancelable: true });
assert.deepStrictEqual(Object.getOwnPropertyNames(event), []);
for (const key of ['type', 'target', 'currentTarget', 'srcElement', 'eventPhase', 'bubbles', 'cancelable', 'defaultPrevented', 'composed', 'timeStamp'])
  assert.strictEqual(accessor(Event.prototype, key), 'gec', key);
assert.strictEqual(accessor(Event.prototype, 'returnValue'), 'gsec');
assert.strictEqual(accessor(Event.prototype, 'cancelBubble'), 'gsec');
assert.strictEqual(accessor(Event.prototype, 'isTrusted'), 'ge-');
assert.deepStrictEqual(
  [event.type, event.target, event.currentTarget, event.eventPhase, event.bubbles, event.cancelable, event.defaultPrevented, event.isTrusted, typeof event.timeStamp],
  ['x', null, null, 0, true, true, false, false, 'number']
);
assert.throws(() => Object.getOwnPropertyDescriptor(Event.prototype, 'type').get.call({}), TypeError);

event.preventDefault();
assert.strictEqual(event.defaultPrevented, true);
assert.strictEqual(event.returnValue, false);
const plain = new Event('y');
plain.preventDefault();
plain.returnValue = false;
assert.strictEqual(plain.defaultPrevented, false);
const viaReturn = new Event('z', { cancelable: true });
viaReturn.returnValue = false;
assert.strictEqual(viaReturn.defaultPrevented, true);
const bubble = new Event('w');
bubble.cancelBubble = true;
assert.strictEqual(bubble.cancelBubble, true);
bubble.cancelBubble = false;
assert.strictEqual(bubble.cancelBubble, true);

const target = new EventTarget();
const seen = [];
target.addEventListener('x', function (e) {
  seen.push(this === target, e.target === target, e.currentTarget === target, e.srcElement === target, e.eventPhase, e.composedPath().length);
  e.stopImmediatePropagation();
});
target.addEventListener('x', () => seen.push('second'));
const dispatched = new Event('x');
assert.strictEqual(target.dispatchEvent(dispatched), true);
assert.deepStrictEqual(seen, [true, true, true, true, 2, 1]);
assert.deepStrictEqual([dispatched.target === target, dispatched.currentTarget, dispatched.eventPhase, dispatched.composedPath().length], [true, null, 0, 0]);

const custom = new CustomEvent('c', { detail: { a: 1 } });
assert.deepStrictEqual(custom.detail, { a: 1 });
assert.strictEqual(new CustomEvent('c').detail, null);
assert.deepStrictEqual(Object.getOwnPropertyNames(custom), []);
assert.strictEqual(accessor(CustomEvent.prototype, 'detail'), 'gec');
assert.throws(() => Object.getOwnPropertyDescriptor(CustomEvent.prototype, 'detail').get.call(new Event('x')), TypeError);

const error = new ErrorEvent('error', { message: 'm', filename: 'f.js', lineno: 3, colno: 4, error: 'E' });
assert.deepStrictEqual([error.message, error.filename, error.lineno, error.colno, error.error], ['m', 'f.js', 3, 4, 'E']);
const bare = new ErrorEvent('error');
assert.deepStrictEqual([bare.message, bare.filename, bare.lineno, bare.colno, bare.error], ['', '', 0, 0, undefined]);

const message = new MessageEvent('message', { data: 'hi' });
assert.deepStrictEqual([message.type, message.data, message.origin, message.lastEventId], ['message', 'hi', '', '']);
assert.deepStrictEqual(Object.getOwnPropertyNames(message), []);
const close = new CloseEvent('close', { code: 1000, reason: 'bye', wasClean: true });
assert.deepStrictEqual([close.code, close.reason, close.wasClean], [1000, 'bye', true]);
assert.deepStrictEqual([new CloseEvent('close').code, new CloseEvent('close').reason, new CloseEvent('close').wasClean], [0, '', false]);

if (typeof PromiseRejectionEvent === 'function') {
  const promise = Promise.resolve(1);
  const rejection = new PromiseRejectionEvent('unhandledrejection', { promise, reason: 'r' });
  assert.strictEqual(rejection.promise, promise);
  assert.strictEqual(rejection.reason, 'r');
}

class Mine extends Event {
  constructor() { super('mine'); this.extra = 1; }
}
const mine = new Mine();
assert.deepStrictEqual([mine.type, mine.extra, mine instanceof Mine, Object.keys(mine)], ['mine', 1, true, ['extra']]);

const reinit = new Event('a');
reinit.initEvent('b', true, true);
assert.deepStrictEqual([reinit.type, reinit.bubbles, reinit.cancelable], ['b', true, true]);

const flat = value => inspect(value).replace(/\s+/g, ' ').replace(/timeStamp: [\d.]+/, 'timeStamp: N');
assert.strictEqual(flat(new Event('x')), "Event { type: 'x', defaultPrevented: false, cancelable: false, timeStamp: N }");
assert.strictEqual(flat(new CustomEvent('c')), "CustomEvent { type: 'c', defaultPrevented: false, cancelable: false, timeStamp: N }");

function readType(e) { return e.type; }
const hot = new Event('hot');
for (let i = 0; i < 20000; i++) readType(hot);
hot.initEvent('cold');
assert.strictEqual(readType(hot), 'cold');

console.log('event prototype getters OK');
