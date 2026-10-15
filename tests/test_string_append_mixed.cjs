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

function fastest(n) {
  let best = Infinity;
  for (let round = 0; round < 3; round++) {
    const started = performance.now();
    const s = mixed(n);
    best = Math.min(best, performance.now() - started);
    assert.strictEqual(s.length, n * 4);
  }
  return best;
}

assert.strictEqual(mixed(100000).slice(-8), 'lit8lit9');
const small = fastest(25000);
const large = fastest(200000);
assert.ok(large / small < 20, `8x more appends took ${(large / small).toFixed(1)}x longer (${small.toFixed(1)} ms vs ${large.toFixed(1)} ms)`);

console.log('string append mixed OK');
