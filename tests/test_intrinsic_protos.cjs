const assert = require('node:assert');
const fs = require('node:fs');

const intrinsics = {
  Error, TypeError, RangeError, AggregateError, Array, Map, Set, Date, RegExp,
  ArrayBuffer, DataView, Uint8Array, Float64Array, Buffer,
};

const saved = {};
for (const name of Object.keys(intrinsics)) {
  saved[name] = Object.getOwnPropertyDescriptor(globalThis, name);
  Object.defineProperty(globalThis, name, { configurable: true, get() { throw new Error(`read global ${name}`); } });
}

let results;
try {
  const typeError = (() => { try { null.x; } catch (e) { return e; } })();
  const rangeError = (() => { try { new intrinsics.Array(-1); } catch (e) { return e; } })();
  results = {
    typeError: Object.getPrototypeOf(typeError) === intrinsics.TypeError.prototype,
    rangeError: Object.getPrototypeOf(rangeError) === intrinsics.RangeError.prototype,
    map: Object.getPrototypeOf(structuredClone(new intrinsics.Map([[1, 2]]))) === intrinsics.Map.prototype,
    set: Object.getPrototypeOf(structuredClone(new intrinsics.Set([1]))) === intrinsics.Set.prototype,
    date: Object.getPrototypeOf(fs.statSync(__filename).mtime) === intrinsics.Date.prototype,
    regexp: Object.getPrototypeOf(/a/.constructor('b')) === intrinsics.RegExp.prototype,
    replace: 'xa'.replace(/a/, 'b') === 'xb',
    buffer: Object.getPrototypeOf(fs.readFileSync(__filename)) === intrinsics.Buffer.prototype,
    isBuffer: intrinsics.Buffer.isBuffer(fs.readFileSync(__filename)),
    float64: Object.getPrototypeOf(intrinsics.Float64Array.of(1)) === intrinsics.Float64Array.prototype,
    arrayBuffer: Object.getPrototypeOf(new intrinsics.Uint8Array(4).buffer) === intrinsics.ArrayBuffer.prototype,
    dataView: Object.getPrototypeOf(new intrinsics.DataView(new intrinsics.ArrayBuffer(4))) === intrinsics.DataView.prototype,
    inspect: require('node:util').inspect(intrinsics.Buffer.from('a')) === '<Buffer 61>',
  };
} finally {
  for (const [name, desc] of Object.entries(saved)) Object.defineProperty(globalThis, name, desc);
}

for (const [name, ok] of Object.entries(results)) assert.ok(ok, name);

assert.strictEqual(AggregateError.prototype.constructor, AggregateError);
assert.ok(new DisposableStack().move() instanceof DisposableStack);
assert.ok(new AsyncDisposableStack().move() instanceof AsyncDisposableStack);

console.log('intrinsic protos OK');
