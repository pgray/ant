const assert = require('node:assert');
const { dlopen, pointer, suffix, FFIType } = require('ant:ffi');

let libcName = `libc.${suffix}`;
if (process.platform === 'darwin') libcName = 'libSystem.dylib';
if (process.platform === 'linux') libcName = 'libc.so.6';
if (process.platform === 'win32') libcName = 'msvcrt.dll';

const libc = dlopen(libcName);
const memset = libc.define('memset', { args: [FFIType.pointer, FFIType.int, FFIType.uint64], returns: FFIType.pointer });

function filled(size, value, options) {
  const buffer = new ArrayBuffer(size, options);
  new Uint8Array(buffer).fill(value);
  return buffer;
}

const detached = { name: 'TypeError', message: 'FFIPointer source buffer is detached' };

for (const [label, detach] of [
  ['transfer()', (buffer) => buffer.transfer()],
  ['transfer(n)', (buffer) => buffer.transfer(32)],
  ['transferToFixedLength()', (buffer) => buffer.transferToFixedLength()],
  ['structuredClone transfer', (buffer) => structuredClone(buffer, { transfer: [buffer] })],
]) {
  const buffer = filled(16, 7);
  const ptr = pointer(buffer);
  const view = pointer(new Uint8Array(buffer, 4));
  const moved = detach(buffer);

  assert.throws(() => ptr.read(FFIType.uint8), detached, `${label}: read`);
  assert.throws(() => ptr.write(FFIType.uint8, 42), detached, `${label}: write`);
  assert.throws(() => view.read(FFIType.uint8), detached, `${label}: view read`);
  assert.throws(() => memset(ptr, 42, 1), detached, `${label}: pointer argument`);
  assert.strictEqual(ptr.isNull(), true, `${label}: isNull`);
  assert.strictEqual(ptr.address(), 0, `${label}: address`);
  assert.strictEqual(new Uint8Array(moved)[0], 7, `${label}: moved buffer must not be reachable`);
}

{
  const buffer = filled(16, 7, { maxByteLength: 64 });
  const ptr = pointer(buffer);
  const tail = ptr.offset(12);

  buffer.resize(4);
  assert.strictEqual(ptr.read(FFIType.uint8), 7);
  assert.throws(() => ptr.offset(10).read(FFIType.uint8), RangeError);
  assert.throws(() => ptr.read(FFIType.int64), RangeError);
  assert.throws(() => ptr.offset(5), RangeError);
  assert.throws(() => tail.read(FFIType.uint8), RangeError);
  assert.throws(() => memset(tail, 0, 1), RangeError);

  buffer.resize(32);
  assert.strictEqual(tail.read(FFIType.uint8), 0, 'grown bytes are zeroed and reachable again');
  ptr.offset(31).write(FFIType.uint8, 9);
  assert.strictEqual(new Uint8Array(buffer)[31], 9);
}

{
  const buffer = filled(16, 7, { maxByteLength: 64 });
  const ptr = pointer(new Uint8Array(buffer, 8, 4));
  buffer.resize(6);
  assert.throws(() => ptr.read(FFIType.uint8), detached, 'out-of-bounds view');
}

{
  const buffer = filled(16, 7);
  const ptr = pointer(buffer);
  let moved;
  assert.throws(() => ptr.write(FFIType.int, { valueOf() { moved = buffer.transfer(); return 42; } }), detached);
  assert.strictEqual(new Uint8Array(moved)[0], 7, 'valueOf detaching during write()');
}

{
  const buffer = filled(16, 7);
  const ptr = pointer(buffer);
  let moved;
  assert.throws(() => ptr.write(FFIType.string, { toString() { moved = buffer.transfer(); return 'x'; } }), detached);
  assert.strictEqual(new Uint8Array(moved)[0], 7, 'toString detaching during write(string)');
}

{
  const buffer = filled(16, 7);
  const ptr = pointer(buffer);
  let moved;
  assert.throws(() => memset(ptr, 42, { valueOf() { moved = buffer.transfer(); return 1; } }), detached);
  assert.strictEqual(new Uint8Array(moved)[0], 7, 'later argument detaching an earlier pointer argument');
}

{
  const buffer = filled(16, 7);
  memset(pointer(buffer).offset(8), 1, 8);
  assert.deepStrictEqual([...new Uint8Array(buffer)], [7, 7, 7, 7, 7, 7, 7, 7, 1, 1, 1, 1, 1, 1, 1, 1]);
}

libc.close();
console.log('FFI pointer source detach ok');
