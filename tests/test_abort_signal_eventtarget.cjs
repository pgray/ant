const assert = require('node:assert');
const { inspect } = require('node:util');

const names = Object.getOwnPropertyNames(AbortSignal.prototype).sort();
assert.deepStrictEqual(names, ['aborted', 'constructor', 'onabort', 'reason', 'throwIfAborted']);
assert.strictEqual(AbortSignal.prototype.addEventListener, EventTarget.prototype.addEventListener);

const controller = new AbortController();
const signal = controller.signal;
assert.deepStrictEqual(Object.getOwnPropertyNames(signal), []);
assert.strictEqual(signal.aborted, false);
assert.strictEqual(signal.reason, undefined);
assert.strictEqual(signal.onabort, null);
assert.strictEqual(Object.prototype.toString.call(signal), '[object AbortSignal]');
assert.strictEqual(inspect(signal), 'AbortSignal { aborted: false }');
assert.throws(() => Object.getOwnPropertyDescriptor(AbortSignal.prototype, 'aborted').get.call({}), TypeError);

const log = [];
signal.onabort = function (event) {
  log.push(['onabort', event.type, event.target === signal, this === signal, event instanceof Event]);
};
signal.addEventListener('abort', function (event) {
  log.push(['listener', event.target === signal, this === signal]);
});
signal.addEventListener('abort', () => log.push(['once']), { once: true });
const removed = () => log.push(['removed']);
signal.addEventListener('abort', removed);
signal.removeEventListener('abort', removed);

controller.abort('why');
controller.abort('again');
assert.deepStrictEqual(log, [['onabort', 'abort', true, true, true], ['listener', true, true], ['once']]);
assert.strictEqual(signal.aborted, true);
assert.strictEqual(signal.reason, 'why');
assert.strictEqual(inspect(signal), 'AbortSignal { aborted: true }');
assert.throws(() => signal.throwIfAborted(), error => error === 'why');

const a = new AbortController();
const b = new AbortController();
const any = AbortSignal.any([a.signal, b.signal]);
let anyFired = 0;
any.addEventListener('abort', () => anyFired++);
b.abort('b');
a.abort('a');
assert.strictEqual(any.aborted, true);
assert.strictEqual(any.reason, 'b');
assert.strictEqual(anyFired, 1);

const pre = AbortSignal.abort();
assert.strictEqual(pre.reason.name, 'AbortError');
assert.ok(pre.reason instanceof DOMException);
assert.strictEqual(AbortSignal.any([pre]).aborted, true);

const manual = new AbortController();
manual.signal.dispatchEvent(new Event('abort'));
assert.strictEqual(manual.signal.aborted, false);

const target = new EventTarget();
let self;
target.addEventListener('x', function () { self = this; });
target.dispatchEvent(new Event('x'));
assert.strictEqual(self, target);
assert.deepStrictEqual(Object.getOwnPropertyNames(new Event('x')).includes('isTrusted'), false);
assert.strictEqual(new Event('x').isTrusted, false);

const viaDispatch = new AbortController();
let handled = 0;
viaDispatch.signal.onabort = () => handled++;
viaDispatch.signal.dispatchEvent(new Event('abort'));
assert.strictEqual(handled, 1);
assert.strictEqual(viaDispatch.signal.aborted, false);

const ordered = new AbortController();
const order = [];
ordered.signal.addEventListener('abort', () => order.push('first'));
ordered.signal.onabort = () => order.push('on');
ordered.signal.addEventListener('abort', () => order.push('last'));
ordered.abort();
assert.deepStrictEqual(order, ['first', 'on', 'last']);

const slot = new AbortController();
const slotOrder = [];
slot.signal.onabort = () => slotOrder.push('on1');
slot.signal.addEventListener('abort', () => slotOrder.push('l'));
slot.signal.onabort = null;
slot.signal.onabort = () => slotOrder.push('on2');
slot.abort();
assert.deepStrictEqual(slotOrder, ['l', 'on2']);

const trusted = new AbortController();
let trustedEvent;
trusted.signal.addEventListener('abort', e => { trustedEvent = e; });
trusted.abort();
assert.strictEqual(trustedEvent.isTrusted, true);
assert.strictEqual(new Event('abort').isTrusted, false);

function readAborted(s) { return s.aborted; }
function readSignal(c) { return c.signal; }
const hot = new AbortController();
for (let i = 0; i < 20000; i++) { readAborted(hot.signal); readSignal(hot); }
hot.abort();
assert.strictEqual(readAborted(hot.signal), true);
assert.strictEqual(readSignal(hot), hot.signal);

const shadowed = new AbortController().signal;
for (let i = 0; i < 20000; i++) readAborted(shadowed);
Object.defineProperty(shadowed, 'aborted', { value: 'own' });
assert.strictEqual(readAborted(shadowed), 'own');

const swapped = new AbortController().signal;
for (let i = 0; i < 20000; i++) readAborted(swapped);
Object.setPrototypeOf(swapped, { aborted: 'other' });
assert.strictEqual(readAborted(swapped), 'other');

Object.defineProperty(AbortSignal.prototype, 'aborted', { get() { return 'redefined'; }, configurable: true });
assert.strictEqual(readAborted(hot.signal), 'redefined');

function readLength(view) { return view.length; }
const bytes = new Uint8Array(8);
for (let i = 0; i < 20000; i++) readLength(bytes);
structuredClone(bytes.buffer, { transfer: [bytes.buffer] });
assert.strictEqual(readLength(bytes), 0);

console.log('abort signal eventtarget OK');
