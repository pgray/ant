const assert = require('node:assert');

const N = 5000;

function sameValues(n) {
  let hits = 0;
  for (let i = 0; i < n; i++) if (Object.is(i % 3, 0)) hits++;
  return hits;
}
assert.strictEqual(sameValues(N), Math.ceil(N / 3));

function parseAll(inputs) {
  let ok = 0, bad = 0;
  for (const s of inputs) {
    try {
      JSON.parse(s);
      ok++;
    } catch (e) {
      assert.ok(e instanceof SyntaxError);
      bad++;
    }
  }
  return [ok, bad];
}
const inputs = [];
for (let i = 0; i < N; i++) inputs.push(i % 10 === 0 ? '{bad' : String(i));
assert.deepStrictEqual(parseAll(inputs), [N - N / 10, N / 10]);

const hasOwn = Object.prototype.hasOwnProperty;
function detached(n) {
  let thrown = 0;
  for (let i = 0; i < n; i++) {
    try {
      hasOwn('x');
    } catch (e) {
      assert.ok(e instanceof TypeError);
      thrown++;
    }
  }
  return thrown;
}
assert.strictEqual(detached(N), N);

function nested(rows) {
  let total = 0;
  for (let i = 0; i < rows.length; i++) {
    const row = rows[i];
    total += row.filter(v => Object.is(v, 1)).length;
    assert.ok(row.hasOwnProperty('length'));
  }
  return total;
}
const rows = [];
for (let i = 0; i < 500; i++) rows.push([1, 2, 1, 3, 1]);
assert.strictEqual(nested(rows), 1500);

class Box {
  constructor(v) { this.v = v; }
  get double() { return this.v * 2; }
}
function thisAcross(items) {
  let sum = 0;
  for (let i = 0; i < items.length; i++) {
    const item = items[i];
    sum += [item].map(function () { return this.double; }, item)[0];
    assert.strictEqual(Object.prototype.toString.call(item), '[object Object]');
  }
  return sum;
}
const boxes = [];
for (let i = 0; i < N; i++) boxes.push(new Box(i));
assert.strictEqual(thisAcross(boxes), N * (N - 1));

async function resolvers(n) {
  const promises = [];
  for (let i = 0; i < n; i++) {
    const { promise, resolve } = Promise.withResolvers();
    resolve(i);
    promises.push(promise);
  }
  const values = await Promise.all(promises);
  return values.reduce((a, b) => a + b, 0);
}

const { Writable } = require('node:stream');
function writeMany(n) {
  let bytes = 0;
  const w = new Writable({ write(chunk, enc, cb) { bytes += chunk.length; cb(); } });
  for (let i = 0; i < n; i++) w.write('ab');
  return bytes;
}
assert.strictEqual(writeMany(N), 2 * N);

resolvers(N).then(sum => {
  assert.strictEqual(sum, N * (N - 1) / 2);
  console.log('ok');
});
