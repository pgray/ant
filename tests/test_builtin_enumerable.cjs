const assert = require('node:assert');
const { inspect } = require('node:util');

const asyncGenProto = Object.getPrototypeOf(async function* () {}).prototype;
const asyncIterProto = Object.getPrototypeOf(Object.getPrototypeOf(asyncGenProto));

const targets = {
  'Number': Number,
  'JSON': JSON,
  'Reflect': Reflect,
  'Atomics': Atomics,
  'Intl': Intl,
  'Intl.Collator.prototype': Intl.Collator.prototype,
  'Intl.NumberFormat.prototype': Intl.NumberFormat.prototype,
  'Intl.DateTimeFormat.prototype': Intl.DateTimeFormat.prototype,
  'Intl.Segmenter.prototype': Intl.Segmenter.prototype,
  'Iterator': Iterator,
  'Iterator.prototype': Iterator.prototype,
  'AsyncIterator.prototype': asyncIterProto,
  'Date.prototype': Date.prototype,
  'RegExp.prototype': RegExp.prototype,
  'Map.prototype': Map.prototype,
  'Set.prototype': Set.prototype,
  'WeakMap.prototype': WeakMap.prototype,
  'WeakSet.prototype': WeakSet.prototype,
  'WeakRef.prototype': WeakRef.prototype,
  'FinalizationRegistry.prototype': FinalizationRegistry.prototype,
  'MapIterator': Object.getPrototypeOf(new Map()[Symbol.iterator]()),
  'SetIterator': Object.getPrototypeOf(new Set()[Symbol.iterator]()),
  'TypedArray.prototype': Object.getPrototypeOf(Uint8Array.prototype),
};

for (const [name, obj] of Object.entries(targets)) {
  const leaked = Reflect.ownKeys(obj).filter(k => Object.getOwnPropertyDescriptor(obj, k).enumerable);
  assert.deepStrictEqual(leaked.map(String), [], name);
}

for (const name of ['Module', 'Instance', 'Memory', 'Table', 'Global', 'CompileError', 'LinkError', 'RuntimeError'])
  assert.strictEqual(Object.getOwnPropertyDescriptor(WebAssembly, name).enumerable, false, `WebAssembly.${name}`);

assert.strictEqual(Object.getOwnPropertyDescriptor(ReadableStream.prototype, Symbol.asyncIterator).enumerable, false);

let count = 0;
for (const k in new Map()) count++;
for (const k in new Set()) count++;
for (const k in /x/) count++;
for (const k in new Date()) count++;
assert.strictEqual(count, 0);

const max = Object.getOwnPropertyDescriptor(Number, 'MAX_SAFE_INTEGER');
assert.deepStrictEqual([max.writable, max.enumerable, max.configurable], [false, false, false]);
assert.strictEqual(Set.prototype.keys, Set.prototype.values);
assert.strictEqual(Map.prototype[Symbol.iterator], Map.prototype.entries);
assert.strictEqual(Object.getPrototypeOf(Uint8Array.prototype)[Symbol.iterator], Object.getPrototypeOf(Uint8Array.prototype).values);
assert.strictEqual(Iterator.prototype.constructor, Iterator);

assert.strictEqual(inspect(RegExp.prototype), '{}');
assert.strictEqual(inspect(Map.prototype), 'Object [Map] {}');
assert.strictEqual(inspect(JSON), 'Object [JSON] {}');
assert.strictEqual(inspect(Math), 'Object [Math] {}');
assert.strictEqual(inspect({ a: 1, [Symbol.toStringTag]: 'T' }), 'Object [T] { a: 1 }');

console.log('builtin enumerable OK');
