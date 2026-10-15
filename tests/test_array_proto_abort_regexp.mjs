import assert from 'node:assert';
import fs from 'node:fs';
import { inspect } from 'node:util';
import { setTimeout as sleep } from 'node:timers/promises';

assert.strictEqual(Object.create([7]).length, 1);
assert.strictEqual(Object.create([7, 8])[1], 8);
const inheritsArray = { __proto__: [1, 2, 3] };
assert.strictEqual(inheritsArray.length, 3);
assert.strictEqual(inheritsArray[0], 1);
assert.deepStrictEqual(Array.from(inheritsArray), [1, 2, 3]);
assert.strictEqual(Reflect.get(Object.create(Object.create([5])), 0), 5);
assert.strictEqual(Object.create([, 1])[0], undefined);

const aborted = AbortSignal.abort('why');
await assert.rejects(fs.promises.readFile(new URL(import.meta.url), { signal: aborted }),
  { name: 'AbortError', code: 'ABORT_ERR', cause: 'why' });
await new Promise(resolve => fs.readFile(new URL(import.meta.url), { signal: aborted }, error => {
  assert.strictEqual(error.code, 'ABORT_ERR');
  resolve();
}));
await assert.rejects(sleep(10, null, { signal: aborted }), { name: 'AbortError', code: 'ABORT_ERR' });

assert.strictEqual(Object.prototype.toString.call(RegExp.prototype), '[object Object]');
assert.strictEqual(Object.prototype.toString.call(/a/), '[object RegExp]');
assert.strictEqual(RegExp.prototype[Symbol.toStringTag], undefined);
assert.strictEqual(inspect(/a\/b/gi), '/a\\/b/gi');
assert.strictEqual(String(new RegExp('')), '/(?:)/');
assert.strictEqual(new RegExp('a/b').source, 'a\\/b');
assert.strictEqual(new RegExp('[/]').source, '[/]');
assert.strictEqual(new RegExp('\n').source, '\\n');

console.log('array proto abort regexp OK');
