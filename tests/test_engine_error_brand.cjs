const assert = require('node:assert/strict');

const throwers = [
  () => null.x,
  () => eval('('),
  () => undefinedBinding,
  () => new Array(-1),
  () => JSON.parse('{'),
];

for (const thrower of throwers) {
  let error;
  try { thrower(); } catch (e) { error = e; }
  assert.equal(Object.prototype.toString.call(error), '[object Error]', String(error));
  const clone = structuredClone(error);
  assert.equal(clone.constructor, error.constructor);
  assert.equal(clone.message, error.message);
}

console.log('engine error brand: ok');
