const assert = require('node:assert');
const { isDeepStrictEqual } = require('node:util');

const wrapper = new String('ab');
assert.strictEqual(wrapper.propertyIsEnumerable('0'), true);
assert.strictEqual(wrapper.propertyIsEnumerable('1'), true);
assert.strictEqual(wrapper.propertyIsEnumerable('2'), false);
assert.strictEqual(wrapper.propertyIsEnumerable('length'), false);

const isEnumerable = Object.prototype.propertyIsEnumerable;
assert.strictEqual(isEnumerable.call('ab', '0'), true);
assert.strictEqual(isEnumerable.call('ab', 'length'), false);
assert.strictEqual(isEnumerable.call('ab', Symbol.iterator), false);

assert.strictEqual(isDeepStrictEqual(new String('a'), new String('a')), true);
assert.strictEqual(isDeepStrictEqual(new String('a'), new String('b')), false);

console.log('string propertyIsEnumerable OK');
