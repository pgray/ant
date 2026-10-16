import assert from 'node:assert';

assert.deepStrictEqual(await Array.fromAsync([1, Promise.resolve(2), 3]), [1, 2, 3]);
assert.deepStrictEqual(await Array.fromAsync([1, 2], async (x, i) => x * 10 + i), [10, 21]);
assert.deepStrictEqual(await Array.fromAsync([1], function (x) { return this.k + x; }, { k: 5 }), [6]);
assert.deepStrictEqual(await Array.fromAsync('a😀b'), ['a', '😀', 'b']);
assert.deepStrictEqual(await Array.fromAsync(new Set([1, 2])), [1, 2]);
assert.deepStrictEqual(await Array.fromAsync({ length: 3, 0: 'a', 1: Promise.resolve('b') }), ['a', 'b', undefined]);

async function* gen() { yield 1; yield 2; }
assert.deepStrictEqual(await Array.fromAsync(gen()), [1, 2]);

const thenable = { then(r) { r(9); } };
const asyncValues = { [Symbol.asyncIterator]() { let i = 0; return { next: async () => ({ done: i > 0, value: i++ === 0 ? thenable : 0 }) }; } };
assert.strictEqual((await Array.fromAsync(asyncValues))[0], thenable);

await assert.rejects(Array.fromAsync(null), TypeError);
await assert.rejects(Array.fromAsync([1], 5), TypeError);
await assert.rejects(Array.fromAsync([Promise.reject(new RangeError('r'))]), RangeError);
await assert.rejects(Array.fromAsync({ [Symbol.iterator]() { return { next() { throw new SyntaxError('nx'); } }; } }), SyntaxError);

class C { constructor(...a) { this.args = a.length; } }
const fromIter = await Array.fromAsync.call(C, [1, 2]);
assert.ok(fromIter instanceof C);
assert.deepStrictEqual([fromIter.args, fromIter.length, fromIter[1]], [0, 2, 2]);
const fromLike = await Array.fromAsync.call(C, { length: 2, 0: 1, 1: 2 });
assert.deepStrictEqual([fromLike.args, fromLike.length], [1, 2]);
assert.ok(Array.isArray(await Array.fromAsync.call({}, [1])));

let closed = 0;
const closable = { [Symbol.asyncIterator]() { return { i: 0, next() { return Promise.resolve({ done: this.i > 2, value: this.i++ }); }, return() { closed++; return Promise.resolve({ done: true }); } }; } };
await assert.rejects(Array.fromAsync(closable, x => { if (x === 1) throw new Error('m'); return x; }), /m/);
assert.strictEqual(closed, 1);

let syncClosed = 0;
const syncIter = { [Symbol.iterator]() { let i = 0; return { next() { return { done: i > 2, value: i++ === 1 ? Promise.reject(new Error('sv')) : i }; }, return() { syncClosed++; return {}; } }; } };
await assert.rejects(Array.fromAsync(syncIter), /sv/);
assert.strictEqual(syncClosed, 1);

const big = Array.from({ length: 3000 }, (_, i) => i);
assert.strictEqual((await Array.fromAsync(big)).length, 3000);

assert.strictEqual(Array.fromAsync.length, 1);
assert.strictEqual(Array.fromAsync.name, 'fromAsync');
assert.strictEqual(Object.getOwnPropertyDescriptor(Array, 'fromAsync').enumerable, false);
assert.ok(Array.fromAsync([]) instanceof Promise);

console.log('Array.fromAsync OK');
