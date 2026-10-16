const assert = require('node:assert');
const strict = require('node:assert/strict');

assert.strictEqual(require('assert/strict'), strict);
assert.strictEqual(assert.strict, strict);
assert.strictEqual(strict.strict, strict);
assert.strictEqual(strict.equal, assert.strictEqual);
assert.strictEqual(strict.notEqual, assert.notStrictEqual);
assert.strictEqual(strict.deepEqual, assert.deepStrictEqual);
assert.strictEqual(strict.notDeepEqual, assert.notDeepStrictEqual);
assert.strictEqual(strict.ok, assert.ok);
assert.strictEqual(strict.AssertionError, assert.AssertionError);
assert.strictEqual(strict.name, 'strict');
assert.strictEqual(strict.equal.name, 'strictEqual');

assert.throws(() => strict.equal(1, '1'), { operator: 'strictEqual' });
assert.throws(() => strict.deepEqual({ a: 1 }, { a: '1' }), { operator: 'deepStrictEqual' });
assert.throws(() => strict.notDeepEqual([1], [1]), { operator: 'notDeepStrictEqual' });
assert.throws(() => strict(0), { message: 'The expression evaluated to a falsy value:\n\n  strict(0)\n' });
strict.equal(1, 1);
strict.deepEqual({ a: [1] }, { a: [1] });

import('node:assert/strict').then(ns => {
  assert.strictEqual(ns.default, strict);
  assert.strictEqual(ns.equal, assert.strictEqual);
  console.log('assert/strict OK');
});
