const assert = require('node:assert');
const { spawnSync } = require('node:child_process');

const addBytes = new Uint8Array([
  0, 97, 115, 109, 1, 0, 0, 0, 1, 7, 1, 96, 2, 127, 127, 1, 127, 3, 2, 1, 0,
  7, 7, 1, 3, 97, 100, 100, 0, 0, 10, 9, 1, 7, 0, 32, 0, 32, 1, 106, 11,
]);

assert.strictEqual(Object.getPrototypeOf(WebAssembly), Object.prototype);
assert.strictEqual(Object.prototype.toString.call(WebAssembly), '[object WebAssembly]');
for (const name of ['Module', 'Instance', 'Memory', 'Table', 'Global', 'Tag', 'Exception'])
  assert.strictEqual(WebAssembly[name].prototype[Symbol.toStringTag], `WebAssembly.${name}`);
assert.strictEqual(Object.prototype.toString.call(new WebAssembly.Memory({ initial: 1 })), '[object WebAssembly.Memory]');

assert.deepStrictEqual(
  [WebAssembly.Module.imports.length, WebAssembly.Module.exports.length, WebAssembly.Module.customSections.length],
  [1, 1, 2]
);
for (const [proto, key] of [
  [WebAssembly.Instance.prototype, 'exports'], [WebAssembly.Memory.prototype, 'buffer'],
  [WebAssembly.Table.prototype, 'length'], [WebAssembly.Global.prototype, 'value'],
]) assert.strictEqual(Object.getOwnPropertyDescriptor(proto, key).enumerable, true, key);

for (const [Ctor, name] of [[WebAssembly.CompileError, 'CompileError'], [WebAssembly.LinkError, 'LinkError'], [WebAssembly.RuntimeError, 'RuntimeError']]) {
  const err = new Ctor('x', { cause: 9 });
  assert.strictEqual(err.message, 'x');
  assert.strictEqual(err.cause, 9);
  assert.strictEqual(err.name, name);
  assert.deepStrictEqual(Object.keys(err), []);
  assert.strictEqual(String(new Ctor()), name);
  assert.strictEqual(Ctor.prototype.name, name);
  assert.strictEqual(Ctor.prototype.message, '');
  assert.strictEqual(Object.getPrototypeOf(Ctor), Error);
  assert.ok(Ctor('y') instanceof Ctor);
}

assert.throws(() => new WebAssembly.Module(new Uint8Array([1, 2, 3])), err =>
  err instanceof WebAssembly.CompileError && /^WebAssembly\.Module\(\): \S/.test(err.message) && !/Failed to compile/.test(err.message));

const quiet = spawnSync(process.execPath, ['-e', 'try { new WebAssembly.Module(new Uint8Array([0,97,115,109,1,0,0,0,99])) } catch {} WebAssembly.validate(new Uint8Array([1]))'], { encoding: 'utf8' });
assert.strictEqual(quiet.stdout, '');
assert.strictEqual(quiet.stderr, '');

const tag = new WebAssembly.Tag({ parameters: ['i32', 'f32', 'i64', 'externref'] });
const payloadObject = {};
const exception = new WebAssembly.Exception(tag, [3.9, 1.1, 5n, payloadObject]);
assert.strictEqual(exception.getArg(tag, 0), 3);
assert.strictEqual(exception.getArg(tag, 1), Math.fround(1.1));
assert.strictEqual(exception.getArg(tag, 2), 5n);
assert.strictEqual(exception.getArg(tag, 3), payloadObject);
assert.strictEqual(exception.is(tag), true);
assert.strictEqual(exception.is(new WebAssembly.Tag({ parameters: [] })), false);
assert.strictEqual(exception.stack, undefined);
assert.strictEqual(typeof new WebAssembly.Exception(tag, [1, 2, 3n, 4], { traceStack: true }).stack, 'string');
assert.throws(() => exception.getArg(tag, 4), RangeError);
assert.throws(() => exception.getArg(new WebAssembly.Tag({ parameters: [] }), 0), TypeError);
assert.throws(() => new WebAssembly.Exception(tag, [1]), TypeError);
assert.throws(() => new WebAssembly.Exception(tag, [1, 2, 3, 4]), TypeError);
assert.throws(() => new WebAssembly.Tag({ parameters: ['bad'] }), TypeError);
assert.ok(WebAssembly.JSTag instanceof WebAssembly.Tag);
assert.throws(() => new WebAssembly.Exception(WebAssembly.JSTag, [1]), TypeError);

const memory = new WebAssembly.Memory({ initial: 1 });
assert.strictEqual(memory.toFixedLengthBuffer(), memory.buffer);
assert.strictEqual(memory.toFixedLengthBuffer().byteLength, 65536);

const addModule = new WebAssembly.Module(addBytes);
assert.strictEqual(Object.getPrototypeOf(WebAssembly.Module.exports(addModule)[0]), Object.prototype);

(async () => {
  const result = await WebAssembly.instantiate(addBytes);
  assert.strictEqual(Object.getPrototypeOf(result), Object.prototype);
  assert.strictEqual(result.instance.exports.add(2, 3), 5);

  const wasmResponse = (type, status = 200) => new Response(addBytes, { status, headers: { 'content-type': type } });
  assert.ok(await WebAssembly.compileStreaming(wasmResponse('application/wasm')) instanceof WebAssembly.Module);
  const streamed = await WebAssembly.instantiateStreaming(Promise.resolve(wasmResponse('Application/Wasm; charset=x')), {});
  assert.strictEqual(streamed.instance.exports.add(4, 5), 9);
  assert.deepStrictEqual(Object.keys(streamed), ['module', 'instance']);

  await assert.rejects(WebAssembly.compileStreaming(wasmResponse('text/plain')), TypeError);
  await assert.rejects(WebAssembly.compileStreaming(wasmResponse('application/wasm', 404)), TypeError);
  await assert.rejects(WebAssembly.compileStreaming(5), TypeError);
  await assert.rejects(WebAssembly.compileStreaming(Promise.reject(new Error('net'))), { message: 'net' });
  await assert.rejects(
    WebAssembly.compileStreaming(new Response(new Uint8Array([0, 97, 115, 109, 1, 0, 0, 0, 99]), { headers: { 'content-type': 'application/wasm' } })),
    err => err instanceof WebAssembly.CompileError && err.message.startsWith('WebAssembly.compileStreaming(): ')
  );

  console.log('wasm js api surface OK');
})();
