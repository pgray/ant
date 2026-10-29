const assert = require('node:assert');

const N = 4000;

function literal(a, b) {
  const o = { x: a, y: b };
  const sum = o.x + o.y;
  return [sum, o.x, o.y];
}
for (let i = 0; i < N; i++) assert.deepStrictEqual(literal(i, 1), [i + 1, i, 1]);
assert.deepStrictEqual(literal('a', 1), ['a1', 'a', 1]);
assert.deepStrictEqual(literal(1.5, 2), [3.5, 1.5, 2]);

function nested(a) {
  const inner = { v: a };
  const outer = { inner, w: a + 1 };
  const t = outer.inner.v * 2 + outer.w;
  return { t, inner: outer.inner, same: outer.inner === inner };
}
for (let i = 0; i < N; i++) assert.deepStrictEqual(nested(i), { t: i * 3 + 1, inner: { v: i }, same: true });
const nestedExit = nested('z');
assert.strictEqual(nestedExit.t, 'NaNz1');
assert.deepStrictEqual(nestedExit.inner, { v: 'z' });
assert.strictEqual(nestedExit.same, true);

function aliased(a) {
  const p = { q: a };
  const alias = p;
  const r = p.q + 1;
  alias.q = r;
  return [p === alias, p.q, alias.q];
}
for (let i = 0; i < N; i++) assert.deepStrictEqual(aliased(i), [true, i + 1, i + 1]);
assert.deepStrictEqual(aliased('s'), [true, 's1', 's1']);

function inner(o, k) { return o.a * k; }
function outerCall(a, k) {
  const o = { a, b: a };
  const r = inner(o, k);
  return [r, o.a, o.b];
}
for (let i = 0; i < N; i++) assert.deepStrictEqual(outerCall(i, 2), [i * 2, i, i]);
assert.deepStrictEqual(outerCall(3, 'x'), [NaN, 3, 3]);

function thrower(o, fail) {
  if (fail) throw new Error('boom ' + o.m);
  return o.m;
}
function caught(a, fail) {
  const o = { m: a, n: a + 1 };
  try {
    return thrower(o, fail) + o.n;
  } catch (e) {
    return e.message + ':' + o.n;
  }
}
for (let i = 0; i < N; i++) assert.strictEqual(caught(i, false), 2 * i + 1);
assert.strictEqual(caught(5, true), 'boom 5:6');

function finallyReturn(a) {
  let log = 0;
  try {
    const o = { a, b: a * 2 };
    return o.a + o.b;
  } finally {
    log++;
  }
}
for (let i = 0; i < N; i++) assert.strictEqual(finallyReturn(i), 3 * i);
assert.strictEqual(finallyReturn('k'), 'kNaN');

function onlyOnExit(a) {
  const o = { a, tag: 'kept' };
  const r = a + 1;
  return r > 0 ? o.a : o;
}
for (let i = 0; i < N; i++) assert.strictEqual(onlyOnExit(i), i);
const kept = onlyOnExit(-5);
assert.deepStrictEqual(kept, { a: -5, tag: 'kept' });
assert.ok(Object.getPrototypeOf(kept) === Object.prototype);

function protoed(a) {
  const o = { __proto__: { inherited: 7 }, own: a };
  const r = o.own + o.inherited;
  return [r, o];
}
for (let i = 0; i < N; i++) assert.strictEqual(protoed(i)[0], i + 7);
const [pr, po] = protoed('x');
assert.strictEqual(pr, 'x7');
assert.strictEqual(po.inherited, 7);

function liveAcrossExit(a) {
  const o = { x: a, y: 1 };
  const r = o.x + 1;
  return r + o.y + o.x;
}
for (let i = 0; i < N; i++) assert.strictEqual(liveAcrossExit(i), 2 * i + 2);
assert.strictEqual(liveAcrossExit('s'), 's11s');
assert.strictEqual(liveAcrossExit(2.5), 7);

function deepLive(a) {
  const leaf = { v: a };
  const mid = { leaf, k: 2 };
  const top = { mid, k: 3 };
  const r = top.mid.leaf.v + top.mid.k;
  return [r, top.mid.leaf.v, top.k, top.mid.leaf === leaf];
}
for (let i = 0; i < N; i++) assert.deepStrictEqual(deepLive(i), [i + 2, i, 3, true]);
assert.deepStrictEqual(deepLive('d'), ['d2', 'd', 3, true]);

function Vec(x, y) { this.x = x; this.y = y; }
Vec.prototype.add = function (o) { return new Vec(this.x + o.x, this.y + o.y); };
Vec.prototype.dot = function (o) { return this.x * o.x + this.y * o.y; };
function vecStep(a, b, k) { return a.add(b).add(new Vec(k, k)).dot(a); }
const va = new Vec(1, 2), vb = new Vec(3, 4);
for (let i = 0; i < N; i++) assert.strictEqual(vecStep(va, vb, i), (4 + i) + 2 * (6 + i));
assert.strictEqual(vecStep(va, vb, 'q'), NaN);

const seen = [];
Object.defineProperty(Vec.prototype, 'y', {
  configurable: true,
  set(v) { seen.push(v); },
  get() { return 100; },
});
assert.strictEqual(vecStep(va, vb, 1), 5 + 2 * 100);
assert.ok(seen.length >= 2);
delete Vec.prototype.y;
for (let i = 0; i < N; i++) assert.strictEqual(vecStep(va, vb, i), (4 + i) + 2 * (6 + i));

let garbage = null;
for (let round = 0; round < 6; round++) {
  for (let i = 0; i < 20000; i++) garbage = { i, next: garbage && i % 64 ? garbage : null, pad: 'p' + i };
  for (let i = 0; i < N; i++) assert.strictEqual(vecStep(va, vb, i), (4 + i) + 2 * (6 + i));
}

let sum = 0;
for (let i = 0; i < N * 10; i++) {
  const p = { x: i, y: i * 2 };
  sum += p.x + p.y;
}
assert.strictEqual(sum, 3 * (N * 10) * (N * 10 - 1) / 2);

console.log('ok');
