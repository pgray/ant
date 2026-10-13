const assert = require('node:assert');

const roots = {
  '%TypedArray%': Object.getPrototypeOf(Uint8Array),
  '%GeneratorPrototype%': Object.getPrototypeOf(function* () {}).prototype,
  EventEmitter: require('node:events'),
  fs: require('node:fs'),
  util: require('node:util'),
  crypto: require('node:crypto'),
  'node:zlib': require('node:zlib'),
  'node:assert': require('node:assert'),
  'node:path': require('node:path'),
  'fs/promises': require('node:fs/promises'),
  'node:stream': require('node:stream'),
  'node:child_process': require('node:child_process'),
};

const resolve = path => path.split('.').reduce((value, key) => value[key], roots[path.split('.')[0]] ? { [path.split('.')[0]]: roots[path.split('.')[0]] } : globalThis);

const expected = [
  ['Object.assign', 2],
  ['Object.defineProperty', 3],
  ['Object.fromEntries', 1],
  ['Object.prototype.hasOwnProperty', 1],
  ['Array.from', 1],
  ['Array.prototype.splice', 2],
  ['Array.prototype.reduce', 1],
  ['Array.prototype.copyWithin', 2],
  ['String.prototype.padStart', 1],
  ['String.prototype.replaceAll', 2],
  ['String.fromCharCode', 1],
  ['Number.prototype.toFixed', 1],
  ['Math.max', 2],
  ['Math.hypot', 2],
  ['JSON.stringify', 3],
  ['Reflect.apply', 3],
  ['Reflect.construct', 2],
  ['Promise.prototype.then', 2],
  ['Promise.all', 1],
  ['Map.prototype.set', 2],
  ['Set.prototype.add', 1],
  ['Date.UTC', 7],
  ['Date.prototype.setHours', 4],
  ['Date.prototype.toJSON', 1],
  ['RegExp.prototype.exec', 1],
  ['%TypedArray%.prototype.subarray', 2],
  ['%TypedArray%.prototype.set', 1],
  ['Atomics.compareExchange', 4],
  ['Atomics.wait', 4],
  ['Iterator.prototype.reduce', 1],
  ['%GeneratorPrototype%.next', 1],
  ['Symbol.for', 1],
  ['BigInt.asIntN', 2],
  ['parseInt', 2],
  ['encodeURIComponent', 1],
  ['setTimeout', 2],
  ['fs.readFile', 3],
  ['fs.writeFileSync', 3],
  ['EventEmitter.prototype.on', 2],
  ['util.inherits', 2],
  ['crypto.createHash', 2],
  ['Buffer.alloc', 3],
  ['Buffer.from', 3],
  ['Buffer.concat', 2],
  ['process.nextTick', 1],
  ['node:zlib.gzipSync', 2],
  ['node:assert.strictEqual', 2],
  ['node:path.join', 0],
  ['AbortSignal.timeout', 1],
  ['DisposableStack.prototype.adopt', 2],
  ['WebAssembly.instantiate', 1],
  ['Function.prototype', 0],
  ['Object', 1],
  ['Function', 1],
  ['Array', 1],
  ['Error', 1],
  ['AggregateError', 2],
  ['SuppressedError', 3],
  ['Promise', 1],
  ['Proxy', 2],
  ['Date', 7],
  ['RegExp', 2],
  ['Map', 0],
  ['WeakRef', 1],
  ['ArrayBuffer', 1],
  ['DataView', 1],
  ['URL', 1],
  ['Event', 1],
  ['ErrorEvent', 2],
  ['File', 2],
  ['Request', 1],
  ['ReadableStreamDefaultReader', 1],
  ['WebAssembly.Exception', 2],
  ['Temporal.PlainDateTime', 3],
  ['fs.access', 3],
  ['fs.opendir', 3],
  ['fs.stat', 1],
  ['fs.realpathSync', 2],
  ['fs/promises.readFile', 2],
  ['fs/promises.access', 1],
  ['node:stream.Readable', 1],
  ['node:child_process.execFile', 4],
  ['util.promisify', 1],
  ['process.hrtime', 1],
];

for (const [path, length] of expected) assert.strictEqual(resolve(path).length, length, path);

const named = [
  ['Function.prototype', ''],
  ['Error', 'Error'],
  ['Promise', 'Promise'],
  ['Symbol', 'Symbol'],
  ['BigInt.asIntN', 'asIntN'],
  ['fs.readFile', 'readFile'],
  ['fs.realpath', 'realpath'],
  ['util.promisify', 'promisify'],
  ['process.hrtime', 'hrtime'],
];

for (const [path, name] of named) {
  const fn = resolve(path);
  assert.ok(Object.hasOwn(fn, 'name'), `${path} own name`);
  assert.strictEqual(fn.name, name, path);
}

for (const fn of [Error, Map, require('node:fs').readFile, URL]) {
  for (const key of ['length', 'name']) {
    const desc = Object.getOwnPropertyDescriptor(fn, key);
    assert.deepStrictEqual([desc.writable, desc.enumerable, desc.configurable], [false, false, true], `${fn.name}.${key}`);
  }
}

assert.throws(() => { 'use strict'; require('node:fs').readFile.name = 'x'; }, TypeError);

assert.deepStrictEqual(Object.keys(Date), []);
assert.deepStrictEqual(Object.keys(Date.prototype), []);

const RealRegExp = RegExp;
Object.defineProperty(globalThis, 'RegExp', { configurable: true, get() { throw new Error('global getter'); } });
try {
  assert.strictEqual(new RealRegExp('a').exec('a')[0], 'a');
  assert.strictEqual('xa'.replace(/a/, 'b'), 'xb');
} finally {
  Object.defineProperty(globalThis, 'RegExp', { configurable: true, writable: true, value: RealRegExp });
}

console.log('builtin lengths OK');
