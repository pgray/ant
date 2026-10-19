const assert = require('node:assert/strict');

const masks = [
  0, 1, 3, 7, 0xff, 0xff00, 0xffff, 0xffff0000, 0x7fffffff, 0x80000000, 0xfffffffe,
  0x55555555, 0xaaaaaaaa, 0x0f0f0f0f, 0xf0f0f0f0, 0x00ff00ff, 0x3ffc, 0x1ffe0, 0x1ffe2,
  0x12345, 0x7f000000, -1, -2, -256, -0x80000000, 0x40000000, 6, 0x2048,
];
const inputs = [0, 1, -1, 2, 0x7fffffff, -0x80000000, 0x12345678, -0x12345678, 0xdeadbeef | 0, 255, 256, 65535, 1 << 30, 3, 5];

function reference(op, mask) {
  if (op === '&') return x => x & mask;
  if (op === '|') return x => x | mask;
  return x => x ^ mask;
}

for (const op of ['&', '|', '^']) {
  for (const mask of masks) {
    const literal = new Function('xs', `let s = 0; for (let i = 0; i < xs.length; i++) s = (s + (xs[i] ${op} ${mask})) | 0; return s;`);
    const viaVar = reference(op, mask);
    let expected = 0;
    for (const x of inputs) expected = (expected + viaVar(x)) | 0;
    for (let r = 0; r < 300; r++) assert.equal(literal(inputs), expected, `${op} ${mask}`);
  }
}

function tagged(values) {
  let n = 0, s = 0, o = 0, a = 0;
  for (const v of values) {
    if (typeof v === 'number') n++;
    else if (typeof v === 'string') s++;
    else if (Array.isArray(v)) a++;
    else if (typeof v === 'object' && v !== null) o++;
  }
  return [n, s, o, a];
}
const mixed = [1, 'a', {}, [], 2.5, 'bc', null, undefined, [1], { x: 1 }, true, -0, NaN, Infinity, -Infinity];
for (let r = 0; r < 500; r++) assert.deepEqual(tagged(mixed), [6, 2, 2, 2]);

function strictTags(values, probe) {
  let hits = 0;
  for (const v of values) if (v === probe) hits++;
  return hits;
}
const probes = [true, false, null, undefined, 0, 'a', -Infinity];
for (const p of probes) for (let r = 0; r < 300; r++) assert.equal(strictTags(mixed, p), mixed.filter(v => Object.is(v, p) || v === p).length);

console.log('ok');
