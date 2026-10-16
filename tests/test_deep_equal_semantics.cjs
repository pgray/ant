const assert = require('node:assert');
const { isDeepStrictEqual } = require('node:util');

const loose = (x, y) => { try { assert.deepEqual(x, y); return true; } catch { return false; } };

const cases = [
  [() => [new Map([[1, 2]]), new Map([[1, 3]])], false, false],
  [() => [new Map([[1, 2]]), new Map([[1, 2]])], true, true],
  [() => [new Map([[{ k: 1 }, 1]]), new Map([[{ k: 1 }, 1]])], true, true],
  [() => [new Map([[{ k: 1 }, 1]]), new Map([[{ k: 1 }, 2]])], false, false],
  [() => [new Map([[1, { a: [1] }]]), new Map([[1, { a: [2] }]])], false, false],
  [() => [new Set([1]), new Set([2])], false, false],
  [() => [new Set([1, 2]), new Set([2, 1])], true, true],
  [() => [new Set([{ a: 1 }]), new Set([{ a: 1 }])], true, true],
  [() => [new Set([{ a: 1 }]), new Set([{ a: 2 }])], false, false],
  [() => [new Date(0), new Date(1)], false, false],
  [() => [new Date(0), new Date(0)], true, true],
  [() => [/a/g, /a/i], false, false],
  [() => [/a/g, /a/g], true, true],
  [() => [new Uint8Array([1, 2]), new Uint8Array([1, 3])], false, false],
  [() => [Buffer.from("ab"), Buffer.from("ab")], true, true],
  [() => [new Uint8Array([1]), new Int8Array([1])], false, false],
  [() => [new Float64Array([0]), new Float64Array([-0])], false, true],
  [() => [new ArrayBuffer(2), new ArrayBuffer(3)], false, false],
  [() => [{ a: 1 }, Object.create(null, { a: { value: 1, enumerable: true } })], false, true],
  [() => [[1, 2], { 0: 1, 1: 2, length: 2 }], false, false],
  [() => [new Number(1), new Number(2)], false, false],
  [() => [new String("a"), new String("a")], true, true],
  [() => [new String("a"), new String("b")], false, false],
  [() => [new Boolean(true), new Boolean(true)], true, true],
  [() => [new Error("a"), new Error("b")], false, false],
  [() => [new TypeError("a"), new Error("a")], false, false],
  [() => [new Error("a"), new Error("a")], true, true],
  [() => [{ [Symbol.for("x")]: 1 }, { [Symbol.for("x")]: 2 }], false, true],
  [() => [[1, , 3], [1, undefined, 3]], false, false],
  [() => [{ a: undefined }, {}], false, false],
  [() => [1, "1"], false, true],
  [() => [NaN, NaN], true, true],
  [() => [0, -0], false, true],
  [() => [null, undefined], false, true],
  [() => [[1, "2"], ["1", 2]], false, true],
  [() => [(() => { const a = { x: 1 }; a.self = a; return a; })(), (() => { const b = { x: 1 }; b.self = b; return b; })()], true, true],
  [() => [(function () { return arguments; })(1), [1]], false, false],
];

for (const [make, strict, isLoose] of cases) {
  const [x, y] = make();
  assert.strictEqual(isDeepStrictEqual(x, y), strict, make.toString());
  assert.strictEqual(loose(x, y), isLoose, make.toString());
}

console.log('deep equal semantics OK');
