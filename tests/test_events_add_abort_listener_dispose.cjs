const assert = require('node:assert');
const { addAbortListener } = require('node:events');

let calls = 0;
const ac = new AbortController();
const disposable = addAbortListener(ac.signal, () => calls++);
assert.strictEqual(typeof disposable[Symbol.dispose], 'function');
assert.strictEqual(Object.hasOwn(disposable, 'dispose'), false);
disposable[Symbol.dispose]();
disposable[Symbol.dispose]();
ac.abort();
assert.strictEqual(calls, 0);

const ac2 = new AbortController();
const dispose = addAbortListener(ac2.signal, () => calls++)[Symbol.dispose];
ac2.abort();
assert.strictEqual(calls, 1);
dispose();

const ac3 = new AbortController();
ac3.abort();
const late = addAbortListener(ac3.signal, () => calls++);
assert.strictEqual(calls, 2);
late[Symbol.dispose]();

{
  using scoped = addAbortListener(new AbortController().signal, () => calls++);
}

console.log('ok');
