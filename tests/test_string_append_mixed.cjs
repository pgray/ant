const assert = require('node:assert');

function mixed(n) {
  let s = '';
  for (let i = 0; i < n; i++) { s += 'lit'; s += i % 10; }
  return s;
}

function stale() {
  let s = '';
  s += 'a';
  s = 3;
  s += 4;
  return s;
}

function viaClosure() {
  let s = '';
  s += 'a';
  const reset = () => { s = 10; };
  reset();
  s += 5;
  return s;
}

assert.strictEqual(mixed(3), 'lit0lit1lit2');
assert.strictEqual(stale(), 7);
assert.strictEqual(viaClosure(), 15);

const started = performance.now();
const big = mixed(100000);
const elapsed = performance.now() - started;
assert.strictEqual(big.length, 400000);
assert.strictEqual(big.slice(-8), 'lit8lit9');
assert.ok(elapsed < 500, `mixed string/number appends took ${elapsed.toFixed(0)} ms`);

console.log('string append mixed OK');
