const assert = require('node:assert');

async function one() { return 1; }

async function constBounds(f) {
  const K = 3000;
  let n = 0;
  for (let i = 0; i < 1000; i++) n += await f();
  for (let i = 0; i < K; i++) n += await f();
  return n;
}

async function twoLoops(k) {
  let total = 0;
  for (let i = 0; i < 2000; i++) total += await one();
  for (let i = 0; i < k; i++) total += await one();
  return total;
}

async function threeLoops(k) {
  let total = 0;
  for (let i = 0; i < 1500; i++) total += await one();
  for (let j = 0; j < 1500; j++) total += (await one()) * 2;
  for (let n = 0; n < k; n++) total += await one();
  return total;
}

async function nested(k) {
  let total = 0;
  for (let i = 0; i < 40; i++) {
    for (let j = 0; j < k; j++) total += await one();
    for (let j = 0; j < k; j++) total -= (await one()) * 0;
  }
  return total;
}

async function forOfLoops(items) {
  let total = 0;
  for (const x of items) total += (await one()) + x;
  for (const x of items) total += await one() * x;
  return total;
}

(async () => {
  assert.strictEqual(await constBounds(() => one()), 4000);
  for (let r = 0; r < 4; r++) {
    assert.strictEqual(await twoLoops(3000), 5000);
    assert.strictEqual(await threeLoops(1000), 1500 + 3000 + 1000);
    assert.strictEqual(await nested(60), 2400);
    assert.strictEqual(await forOfLoops([1, 2, 3, 4, 5, 6, 7, 8]), 8 + 36 + 36);
  }
  console.log('ok');
})();
