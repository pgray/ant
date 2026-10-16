const assert = require('node:assert');

const a = [1];
a.push(a);
assert.strictEqual(String(a), '1,');
assert.strictEqual([[a]].join('-'), '1,');
assert.strictEqual(a.toString(), '1,');
assert.strictEqual(a.toLocaleString(), '1,');
assert.strictEqual([a, a].join('|'), '1,|1,');
assert.strictEqual(String([1, [2, [3]]]), '1,2,3');

const arrayLike = { length: 2, 0: null, 1: 'x', toString: Array.prototype.join };
arrayLike[0] = arrayLike;
assert.strictEqual(String(arrayLike), ',x');

const mutual = [1, 2];
mutual[1] = [mutual, 3];
assert.strictEqual(String(mutual), '1,,3');

const throwing = [{ toString() { throw new Error('boom'); } }];
assert.throws(() => String(throwing), /boom/);
throwing.push(throwing);
assert.throws(() => String([throwing]), /boom/);
assert.strictEqual(String(a), '1,');

console.log('array join cycles OK');
