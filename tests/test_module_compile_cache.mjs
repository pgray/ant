import assert from 'node:assert';
import module, { enableCompileCache, getCompileCacheDir, flushCompileCache, constants } from 'node:module';

const { FAILED, ENABLED, ALREADY_ENABLED, DISABLED } = constants.compileCacheStatus;
assert.deepStrictEqual([FAILED, ENABLED, ALREADY_ENABLED, DISABLED], [0, 1, 2, 3]);

const result = enableCompileCache();
assert.strictEqual(typeof result, 'object');
assert.ok(Object.values(constants.compileCacheStatus).includes(result.status));
if (result.status === DISABLED || result.status === FAILED) assert.strictEqual(typeof result.message, 'string');

assert.strictEqual(module.enableCompileCache, enableCompileCache);
assert.strictEqual(flushCompileCache(), undefined);
const dir = getCompileCacheDir();
assert.ok(dir === undefined || typeof dir === 'string');

console.log('module compile cache: ok');
