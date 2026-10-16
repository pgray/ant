const assert = require('node:assert');

const order = r => Object.keys(r).join(',');
assert.strictEqual(order([1].values().next()), 'value,done');
assert.strictEqual(order(new Map([[1, 2]]).keys().next()), 'value,done');
assert.strictEqual(order(new Set([1]).values().next()), 'value,done');
assert.strictEqual(order('a'[Symbol.iterator]().next()), 'value,done');
assert.strictEqual(order((function* () { yield 1; })().next()), 'value,done');
assert.strictEqual(order([1].values().map(x => x).next()), 'value,done');
assert.deepStrictEqual([1].values().next(), { value: 1, done: false });
assert.deepStrictEqual((function* () { return 7; })().next(), { value: 7, done: true });

(async () => {
  assert.strictEqual(order(await (async function* () { yield 1; })().next()), 'value,done');
  const a = [1].values().next(), b = [1].values().next();
  assert.notStrictEqual(a, b);
  console.log('iterator result shape OK');
})();
