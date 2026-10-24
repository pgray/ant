const assert = require('node:assert');

const wasmMemory = () => {
  const memory = new WebAssembly.Memory({ initial: 64, maximum: 1024 });
  let grown = false;
  const grow = () => { if (!grown) memory.grow(512); grown = true; };
  return { memory, grow, view: new Uint8Array(memory.buffer) };
};

const transferred = () => {
  const buffer = new ArrayBuffer(64);
  return { buffer, detach: () => buffer.transfer(), view: new Uint8Array(buffer) };
};

const resizable = () => {
  const buffer = new ArrayBuffer(64, { maxByteLength: 64 });
  return { buffer, shrink: () => buffer.resize(0), view: new Uint8Array(buffer) };
};

const coerce = (effect, result = 0x41) => ({
  valueOf() { effect(); return result; },
  toString() { effect(); return 'latin1'; },
});

{
  const { grow, view } = wasmMemory();
  assert.throws(() => view.fill(coerce(grow)), TypeError);
}

{
  const { grow, view } = wasmMemory();
  assert.strictEqual(view.set([coerce(grow), 1, 2]), undefined);
}

{
  const { grow, view } = wasmMemory();
  assert.strictEqual(view.indexOf(coerce(grow)), -1);
}

{
  const { grow, view } = wasmMemory();
  assert.strictEqual(view.indexOf(0x41, coerce(grow, 0)), -1);
}

{
  const { memory, grow } = wasmMemory();
  try { Buffer.from(memory.buffer).fill(coerce(grow)); } catch {}
}

{
  const { memory, grow } = wasmMemory();
  assert.strictEqual(Buffer.from(memory.buffer).indexOf(0x41, coerce(grow, 0)), -1);
}

{
  const { memory, grow } = wasmMemory();
  assert.strictEqual(Buffer.from(memory.buffer).indexOf('A', 0, coerce(grow, 'latin1')), -1);
}

{
  const { memory, grow } = wasmMemory();
  const encoding = { toString() { grow(); return 'latin1'; } };
  assert.strictEqual(Buffer.from(memory.buffer).write('A'.repeat(4096), 0, 4096, encoding), 0);
}

{
  const buf = Buffer.alloc(64);
  for (const value of [undefined, 123, { toString: () => 'ab' }]) {
    assert.throws(() => buf.write(value, 0, 64, 'latin1'), { code: 'ERR_INVALID_ARG_TYPE' });
  }
  assert.throws(() => buf.write(), { code: 'ERR_INVALID_ARG_TYPE' });
}

{
  let moved;
  const { buffer, view } = transferred();
  assert.throws(() => view.fill(coerce(() => { moved = buffer.transfer(); })), TypeError);
  assert.deepStrictEqual([...new Uint8Array(moved)], new Array(64).fill(0));
}

{
  let moved;
  const { buffer, view } = transferred();
  view.set([coerce(() => { moved = buffer.transfer(); }), 1]);
  assert.deepStrictEqual([...new Uint8Array(moved)], new Array(64).fill(0));
}

{
  const { buffer, shrink, view } = resizable();
  assert.strictEqual(view.indexOf(0, coerce(shrink, 0)), -1);
  buffer.resize(64);
  assert.strictEqual(view.fill(coerce(shrink)), view);
  assert.strictEqual(view.length, 0);
  buffer.resize(64);
  assert.deepStrictEqual([...view], new Array(64).fill(0));
}

{
  const { detach, view } = transferred();
  assert.strictEqual(view.indexOf(coerce(detach)), -1);
}

{
  const view = new Uint8Array(8);
  assert.strictEqual(view.fill({ valueOf: () => 7 }, 2, 4), view);
  assert.deepStrictEqual([...view], [0, 0, 7, 7, 0, 0, 0, 0]);
  assert.deepStrictEqual([...Buffer.alloc(3).fill({ valueOf: () => 0x141 })], [0x41, 0x41, 0x41]);
  assert.strictEqual(Buffer.from('xxAx').indexOf('A', { valueOf: () => 1 }), 2);
}

{
  const { grow, view } = wasmMemory();
  assert.strictEqual(view.includes(0x41, coerce(grow, 0)), false);
}

{
  const calls = [];
  const encoding = (result) => ({
    valueOf() { calls.push('valueOf'); return result; },
    toString() { calls.push('toString'); return 'utf8'; },
  });
  const buf = Buffer.from('xxAx');
  assert.strictEqual(buf.indexOf('A', 0, encoding('latin1')), 2);
  assert.strictEqual(buf.lastIndexOf('A', 3, encoding('latin1')), 2);
  assert.strictEqual(buf.includes(Buffer.from('A'), 0, encoding('latin1')), true);
  assert.deepStrictEqual(calls, ['valueOf', 'valueOf', 'valueOf']);
  assert.throws(() => buf.indexOf('A', 0, encoding(65)), { code: 'ERR_UNKNOWN_ENCODING' });
  calls.length = 0;
  assert.strictEqual(buf.indexOf(0x41, 0, encoding('latin1')), 2);
  assert.deepStrictEqual(calls, []);
  assert.throws(() => buf.indexOf('A', 0, null), { code: 'ERR_UNKNOWN_ENCODING' });
  assert.throws(() => buf.indexOf('A', 0, 5), { code: 'ERR_UNKNOWN_ENCODING' });
}

{
  let coerced = false;
  const needle = { valueOf() { coerced = true; return 1; } };
  for (const method of ['indexOf', 'includes']) {
    const miss = method === 'indexOf' ? -1 : false;
    const hit = (index) => method === 'indexOf' ? index : true;
    assert.strictEqual(new Uint8Array([1])[method]('1'), miss);
    assert.strictEqual(new Uint8Array([1])[method](true), miss);
    assert.strictEqual(new Uint8Array([1])[method](needle), miss);
    assert.strictEqual(new Uint8Array([1])[method](257), miss);
    assert.strictEqual(new Int8Array([1])[method](1.5), miss);
    assert.strictEqual(new Int16Array([-1])[method](-1), hit(0));
    assert.strictEqual(new Uint32Array([0xffffffff])[method](-1), miss);
    assert.strictEqual(new Uint32Array([0xffffffff])[method](0xffffffff), hit(0));
    assert.strictEqual(new Float32Array([0.1])[method](0.1), miss);
    assert.strictEqual(new Float32Array([0.5])[method](0.5), hit(0));
    assert.strictEqual(new Float64Array([0])[method](-0), hit(0));
    assert.strictEqual(new Int32Array([1])[method](1n), miss);
    assert.strictEqual(new BigInt64Array([1n])[method](1), miss);
    assert.strictEqual(new BigInt64Array([-1n])[method](-1n), hit(0));
    assert.strictEqual(new BigUint64Array([1n])[method](2n ** 64n + 1n), miss);
    assert.strictEqual(new Uint8Array([1, 2, 1])[method](1, 1), hit(2));
    assert.strictEqual(new Uint8Array([1, 2])[method](1, -1), miss);
    assert.strictEqual(new Uint8Array([1, 2])[method](1, Infinity), miss);
    assert.strictEqual(new Uint8Array([1, 2])[method](1, -Infinity), hit(0));
  }
  assert.strictEqual(coerced, false);
  assert.strictEqual(new Float64Array([NaN]).indexOf(NaN), -1);
  assert.strictEqual(new Float64Array([NaN]).includes(NaN), true);
  if (typeof Float16Array === 'function') assert.strictEqual(new Float16Array([NaN]).includes(NaN), true);
  assert.strictEqual(new Uint8Array([0]).includes(NaN), false);
  assert.strictEqual(new Uint8Array([0]).includes(), false);
  assert.strictEqual(new Float64Array([NaN]).includes(), false);
}

console.log('ok');
