const assert = require('node:assert');

function m(a, b) { return a % b; }
function caller(a, b) { let r; for (let i = 0; i < 2; i++) r = m(a, b); return r; }
function direct(a, b) { let r; for (let i = 0; i < 2; i++) r = a % b; return r; }

const expected = [
  [1e17, 3, 1],
  [-4, 2, -0],
  [5, 0, NaN],
  [1e300, 7, 1],
  [0, 7, 0],
  [-0, 7, -0],
  [3, -0, NaN],
  [2 ** 53 - 1, 7, 3],
  [2 ** 53 - 1, -(2 ** 53), 2 ** 53 - 1],
  [2 ** 53, 7, 4],
  [2 ** 53 - 1, 2 ** 63, 2 ** 53 - 1],
  [2 ** 53 - 1, -(2 ** 63), 2 ** 53 - 1],
  [7, 2 ** 64, 7],
  [2 ** 63, 7, 1],
  [-(2 ** 63), 7, -1],
  [5.5, 2, 1.5],
  [-5, 3, -2],
  [5, -3, 2],
  [4294967296, 7, 4],
  [2147483647, -2147483648, 2147483647],
  [Infinity, 3, NaN],
  [3, Infinity, 3],
  [NaN, 3, NaN],
  [-14, 7, -0],
  [-12.75, 5, -2.75],
  [-0.5, 1, -0.5],
  [12.75, -5, 2.75],
  [-12.75, -5, -2.75],
  [-(2 ** 53 - 1), 7, -3],
  [-(2 ** 53), 7, -4],
  [2 ** 52 - 0.5, 3, 0.5],
  [0.9999999999999999, 1, 0.9999999999999999],
  [-1e-300, 3, -1e-300],
  [370.25, 360, 10.25],
  [0.5, 2 ** 63, 0.5],
  [-0.5, -(2 ** 63), -0.5],
  [NaN, 0.5, NaN],
];

const values = [0, -0, 1, -1, 2, -4, 3, 7, -7, 7.5, -7.5, 0.1, 0.3, 1e17, -1e17, 2 ** 53 - 1, 2 ** 53, 2 ** 53 + 2, -(2 ** 53), 2 ** 63, -(2 ** 63), 2 ** 64, 1e300, -1e300, 5e-324, Infinity, -Infinity, NaN, 2147483647, -2147483648, 4294967296];
const pairs = expected.map(([a, b]) => [a, b]);
for (const a of values) for (const b of values) pairs.push([a, b]);

for (let warm = 0; warm < 200; warm++) for (const [a, b] of pairs) { caller(a, b); direct(a, b); }

for (const [a, b, want] of expected)
  for (const f of [caller, direct]) assert.ok(Object.is(f(a, b), want), `${f.name} ${a} % ${b}: got ${f(a, b)}, expected ${want}`);

for (const [a, b] of pairs)
  for (const f of [caller, direct]) assert.ok(Object.is(f(a, b), a % b), `${f.name} ${a} % ${b}: got ${f(a, b)}, expected ${a % b}`);

console.log('jit inline mod OK');
