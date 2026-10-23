const assert = require('node:assert');
const { pointer, FFIType } = require('ant:ffi');

const SIZE = 4096;
const COUNT = 64;

function churn(n) {
  let junk = null;
  for (let i = 0; i < n; i++) junk = { i, next: junk && i % 64 ? junk : null };
  return junk;
}

function fresh(seed) {
  const bytes = new Uint8Array(SIZE);
  bytes.fill(seed);
  return bytes;
}

const direct = [];
const derived = [];
for (let i = 0; i < COUNT; i++) {
  direct.push(pointer(fresh(i + 1)));
  derived.push(pointer(fresh(i + 101)).offset(SIZE - 1));
}

for (let round = 0; round < 8; round++) {
  const filler = [];
  for (let i = 0; i < COUNT * 2; i++) filler.push(new Uint8Array(SIZE).fill(0xee));
  churn(200000);
}

for (let i = 0; i < COUNT; i++) {
  assert.strictEqual(direct[i].read(FFIType.uint8), i + 1, `pointer(buf) #${i} lost its source`);
  assert.strictEqual(direct[i].offset(SIZE - 1).read(FFIType.uint8), i + 1);
  assert.strictEqual(derived[i].read(FFIType.uint8), i + 101, `pointer(buf).offset() #${i} lost its source`);
  derived[i].write(FFIType.uint8, 7);
  assert.strictEqual(derived[i].read(FFIType.uint8), 7);
}

console.log('FFI pointer roots source ok');
