import assert from 'node:assert';
import { createRequire } from 'node:module';
import order from 'web-order';
import cond from 'web-cond';
import field from 'web-field';

const require = createRequire(import.meta.url);
assert.strictEqual(order, 'node');
assert.strictEqual(require('web-order'), 'node');
assert.strictEqual(cond, 'node');
assert.strictEqual(field, 'main');
assert.strictEqual(import.meta.resolve('web-order').split('/').pop(), 'node.js');

console.log('exports condition order OK');
