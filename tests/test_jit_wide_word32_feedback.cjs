'use strict';
const assert = require('node:assert');

function int32(x) { return Number(BigInt.asIntN(32, BigInt(Math.trunc(x) + 0))); }
function uint32(x) { return Number(BigInt.asUintN(32, BigInt(Math.trunc(x) + 0))); }

function reference(x, y) {
  if (!Number.isFinite(x)) x = 0;
  const a = int32(x), b = int32(y), s = uint32(y) & 31;
  return [a & b, a | b, a ^ b, int32(a * 2 ** s), a >> s, uint32(x) >>> s, ~a];
}

function wideMix(x, y) { return [x & y, x | y, x ^ y, x << y, x >> y, x >>> y, ~x]; }

const wide = [];
for (let i = 0; i < 64; i++) {
  wide.push((i + 1) * 0x9e3779b1 * 4099 + i / 8);
  wide.push(-(i + 3) * 0x85ebca6b * 8191 - i / 4);
}
for (let i = 0; i < 2000; i++) {
  const x = wide[i % wide.length], y = wide[(i * 7 + 3) % wide.length];
  assert.deepStrictEqual(wideMix(x, y), reference(x, y), `wide inputs ${x}, ${y}`);
}

const edges = [
  [2 ** 32, 2 ** 32 + 5], [2 ** 53 - 1, -(2 ** 53) + 1],
  [9223372036854774784, -9223372036854774784],
  [2 ** 63, 3], [-(2 ** 63), 3], [2 ** 64, 2 ** 64], [1e300, -1e300],
  [NaN, 2 ** 40], [Infinity, 1], [-Infinity, 1], [-0, 0],
];
for (const [x, y] of edges) {
  for (let i = 0; i < 50; i++)
    assert.deepStrictEqual(wideMix(x, y), reference(x, y), `edge inputs ${x}, ${y}`);
}
assert.deepStrictEqual(wideMix('4294967301', true), [1, 5, 4, 10, 2, 2, -6]);
const events = [];
const left = { valueOf() { events.push('left'); return 2 ** 40 + 6; } };
const right = { valueOf() { events.push('right'); return 3; } };
assert.strictEqual(wideMix(left, right)[0], 2);
assert.strictEqual(events.slice(0, 2).join(','), 'left,right');

function djb2(s) {
  let h = 5381;
  for (let i = 0; i < s.length; i++) h = ((h << 5) - h + s.charCodeAt(i)) | 0;
  return h;
}
function djb2Reference(s) {
  let h = 5381n;
  for (let i = 0; i < s.length; i++) h = BigInt.asIntN(32, h * 31n + BigInt(s.charCodeAt(i)));
  return Number(h);
}
const text = 'the quick brown fox jumps over the lazy dog '.repeat(40);
for (let i = 0; i < 300; i++) {
  const s = text.slice(i % 40, 200 + (i % 97));
  assert.strictEqual(djb2(s), djb2Reference(s));
}

function hugeMix(x, y) { return [x & y, x | y, x ^ y, x << y, x >> y, x >>> y, ~x]; }
for (let i = 0; i < 2000; i++) {
  const x = 2 ** 64 * (1 + (i & 7)), y = i & 31;
  assert.deepStrictEqual(hugeMix(x, y), reference(x, y), `huge inputs ${x}, ${y}`);
}

console.log('PASS wide word32 feedback');
