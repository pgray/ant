const assert = require('node:assert');

const N = 20000;
let fired = 0, bad = 0;

function churn(n) {
  let keep = null;
  for (let i = 0; i < n; i++) {
    const o = { i, s: 'x' + i, a: [i] };
    if ((i & 4095) === 0) keep = o;
  }
  return keep;
}

for (let i = 0; i < N; i++) {
  const payload = { id: i, tag: 'p' + i, list: [i, i * 2] };
  const extra = { v: i * 3 };
  setTimeout((p, e) => {
    fired++;
    if (p.id !== i || p.tag !== 'p' + i || p.list[1] !== i * 2 || e.v !== i * 3) bad++;
  }, 20 + (i % 20), payload, extra);
  if ((i & 1023) === 0) churn(20000);
}
churn(1e6);

let refreshed = 0;
const reloaded = setTimeout(o => { refreshed += o.k; }, 5, { k: 7 });
setTimeout(() => { churn(5e5); reloaded.refresh(); churn(5e5); }, 10);

setTimeout(() => {
  churn(1e6);
  assert.strictEqual(fired, N);
  assert.strictEqual(bad, 0);
  assert.strictEqual(refreshed, 14);
  console.log('timer minor gc OK');
}, 300);
