const assert = require('node:assert');

const kinds = [
  Object.getPrototypeOf(function* () {}).constructor,
  Object.getPrototypeOf(async function () {}).constructor,
  Object.getPrototypeOf(async function* () {}).constructor,
];

for (const Kind of kinds) {
  assert.strictEqual(Object.getPrototypeOf(Kind), Function, Kind.name);
  assert.strictEqual(Object.getPrototypeOf(Kind.prototype), Function.prototype, Kind.name);
  assert.strictEqual(Kind.prototype.constructor, Kind, Kind.name);
  assert.strictEqual(typeof Kind.call, 'function', Kind.name);
}

console.log('function kind constructors OK');
