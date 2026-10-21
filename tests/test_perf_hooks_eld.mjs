import assert from 'node:assert/strict';
import { monitorEventLoopDelay } from 'node:perf_hooks';

const h = monitorEventLoopDelay({ resolution: 10 });
assert.equal(h.count, 0);
assert.equal(h.max, 0);
assert.equal(h.min, 9223372036854775807);
assert.ok(Number.isNaN(h.mean));
assert.ok(Number.isNaN(h.stddev));
assert.equal(h.exceeds, 0);

assert.equal(h.enable(), true);
assert.equal(h.enable(), false);
await new Promise(r => setTimeout(r, 150));
assert.equal(h.disable(), true);
assert.equal(h.disable(), false);

assert.ok(h.count > 0);
assert.ok(h.min >= 5e6, `min ${h.min}`);
assert.ok(h.max >= h.min);
assert.ok(h.mean >= h.min && h.mean <= h.max);
assert.ok(h.stddev >= 0);
assert.equal(h.percentile(100), h.max);
assert.ok(h.percentile(50) >= h.min && h.percentile(50) <= h.max);
assert.equal(typeof h.percentiles.get, 'function');
assert.ok(Math.abs(h.percentiles.get(100) - h.max) <= h.max * 0.01);
assert.equal(typeof h.maxBigInt, 'bigint');
assert.equal(h.countBigInt, BigInt(h.count));
assert.equal(typeof h.percentileBigInt(99), 'bigint');

const counted = h.count;
await new Promise(r => setTimeout(r, 40));
assert.equal(h.count, counted);

h.reset();
assert.equal(h.count, 0);
assert.equal(h.max, 0);

for (const bad of [0, -1, 1.5]) assert.throws(() => monitorEventLoopDelay({ resolution: bad }), { code: 'ERR_OUT_OF_RANGE' });
assert.throws(() => monitorEventLoopDelay({ resolution: 'x' }), { code: 'ERR_INVALID_ARG_TYPE' });
assert.throws(() => h.percentile(0), { code: 'ERR_OUT_OF_RANGE' });
assert.throws(() => h.percentile(101), { code: 'ERR_OUT_OF_RANGE' });

const idle = monitorEventLoopDelay();
idle.enable();

console.log('perf_hooks event loop delay: ok');
