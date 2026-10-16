import assert from 'node:assert';

async function* values() { const x = await 1; yield x; yield await Promise.resolve(2); yield Promise.resolve(3); yield 4; }
const it = values();
assert.deepStrictEqual(
  [await it.next(), await it.next(), await it.next(), await it.next(), await it.next()],
  [{ value: 1, done: false }, { value: 2, done: false }, { value: 3, done: false }, { value: 4, done: false }, { value: undefined, done: true }]
);

async function* rejected() {
  try { yield Promise.reject(new Error('boom')); } catch (e) { yield 'caught ' + e.message; }
}
const seen = [];
for await (const v of rejected()) seen.push(v);
assert.deepStrictEqual(seen, ['caught boom']);

async function* primitives() { for (const v of [null, undefined, 0, '', false, 1n]) yield await v; }
const prim = [];
for await (const v of primitives()) prim.push(v);
assert.deepStrictEqual(prim, [null, undefined, 0, '', false, 1n]);

console.log('async generator yield await OK');
