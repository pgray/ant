const assert = require('node:assert');

const target = {};
let current = 'a';
Object.defineProperty(target, 'x', { get: () => current, enumerable: true });

const cache = new Map();
const cached = new Proxy(target, {
  get(t, k) {
    if (!cache.has(k)) cache.set(k, t[k]);
    return cache.get(k);
  },
});
assert.strictEqual(cached.x, 'a');
current = 'b';
assert.strictEqual(cached.x, 'a');

const setterOnly = {};
Object.defineProperty(setterOnly, 'y', { set() {} });
assert.throws(() => new Proxy(setterOnly, { get: () => 1 }).y, TypeError);
assert.strictEqual(new Proxy(setterOnly, { get: () => undefined }).y, undefined);

const frozen = {};
Object.defineProperty(frozen, 'z', { value: 1 });
assert.throws(() => new Proxy(frozen, { get: () => 2 }).z, TypeError);
assert.strictEqual(new Proxy(frozen, { get: () => 1 }).z, 1);

const nan = {};
Object.defineProperty(nan, 'n', { value: NaN });
assert.ok(Number.isNaN(new Proxy(nan, { get: () => NaN }).n));
const negZero = {};
Object.defineProperty(negZero, 'm', { value: -0 });
assert.throws(() => new Proxy(negZero, { get: () => 0 }).m, TypeError);

const accessorSet = {};
Object.defineProperty(accessorSet, 'w', { get: () => 1, set() {} });
const setProxy = new Proxy(accessorSet, { set: () => true });
setProxy.w = 5;

const getterOnly = {};
Object.defineProperty(getterOnly, 'v', { get: () => 1 });
assert.throws(() => { 'use strict'; new Proxy(getterOnly, { set: () => true }).v = 2; }, TypeError);

const readonly = {};
Object.defineProperty(readonly, 'r', { value: NaN });
const readonlyProxy = new Proxy(readonly, { set: () => true });
readonlyProxy.r = NaN;
assert.throws(() => { 'use strict'; readonlyProxy.r = 1; }, TypeError);

const hasNonConfig = {};
Object.defineProperty(hasNonConfig, 'h', { get: () => 1 });
assert.throws(() => 'h' in new Proxy(hasNonConfig, { has: () => false }), TypeError);
const hasSealed = Object.preventExtensions({ s: 1 });
assert.throws(() => 's' in new Proxy(hasSealed, { has: () => false }), TypeError);
assert.strictEqual('missing' in new Proxy(hasSealed, { has: () => false }), false);
assert.strictEqual('c' in new Proxy({ c: 1 }, { has: () => false }), false);

const computed = new Proxy(target, { get: (t, k) => t[k] });
const xKey = ['x'][0];
assert.strictEqual(computed[xKey], 'b');
assert.throws(() => new Proxy(frozen, { get: () => 2 })[['z'][0]], TypeError);

const sym = Symbol('s');
const symTarget = {};
Object.defineProperty(symTarget, sym, { get: () => 1 });
assert.strictEqual(new Proxy(symTarget, { get: () => 7 })[sym], 7);
const symSetterOnly = {};
Object.defineProperty(symSetterOnly, sym, { set() {} });
assert.throws(() => new Proxy(symSetterOnly, { get: () => 1 })[sym], TypeError);
const symNaN = {};
Object.defineProperty(symNaN, sym, { value: NaN });
new Proxy(symNaN, { set: () => true })[sym] = NaN;
assert.throws(() => { 'use strict'; new Proxy(symNaN, { set: () => true })[sym] = 1; }, TypeError);

assert.throws(() => sym in new Proxy(symTarget, { has: () => false }), TypeError);
assert.throws(() => ['h'][0] in new Proxy(hasNonConfig, { has: () => false }), TypeError);
assert.throws(() => delete new Proxy(symTarget, { deleteProperty: () => true })[sym], TypeError);
assert.throws(() => delete new Proxy(hasNonConfig, { deleteProperty: () => true })[['h'][0]], TypeError);
assert.strictEqual(delete new Proxy({ d: 1 }, { deleteProperty: () => true })[['d'][0]], true);

const explicitUndefGet = {};
Object.defineProperty(explicitUndefGet, 'v', { get: undefined, set(v) {} });
assert.throws(() => new Proxy(explicitUndefGet, { get: () => 3 }).v, TypeError);
assert.strictEqual(new Proxy(explicitUndefGet, { get: () => undefined }).v, undefined);
new Proxy(explicitUndefGet, { set: () => true }).v = 1;

const explicitUndefSet = {};
Object.defineProperty(explicitUndefSet, 'v', { get() { return 1; }, set: undefined });
assert.throws(() => { 'use strict'; new Proxy(explicitUndefSet, { set: () => true }).v = 2; }, TypeError);
assert.strictEqual(new Proxy(explicitUndefSet, { get: () => 9 }).v, 9);

const explicitUndefBoth = {};
Object.defineProperty(explicitUndefBoth, 'u', { get: undefined, set: undefined });
assert.throws(() => new Proxy(explicitUndefBoth, { get: () => 1 }).u, TypeError);
assert.throws(() => { 'use strict'; new Proxy(explicitUndefBoth, { set: () => true }).u = 1; }, TypeError);

const deleteSealed = Object.preventExtensions({ a: 1 });
assert.throws(() => delete new Proxy(deleteSealed, { deleteProperty: () => true }).a, TypeError);
assert.throws(() => delete new Proxy(deleteSealed, { deleteProperty: () => true })[['a'][0]], TypeError);
assert.strictEqual(delete new Proxy(deleteSealed, { deleteProperty: () => true }).missing, true);
const deleteSealedSym = Object.preventExtensions({ [sym]: 1 });
assert.throws(() => delete new Proxy(deleteSealedSym, { deleteProperty: () => true })[sym], TypeError);

const longKey = 'k'.repeat(300);
const longTarget = { [longKey]: 1 };
assert.strictEqual(longKey in new Proxy(longTarget, {}), true);
assert.strictEqual(('k'.repeat(299) + 'j') in new Proxy(longTarget, {}), false);
assert.strictEqual(new Proxy(longTarget, {})[longKey], 1);

let handlerGets = 0;
const countingHandler = { get get() { handlerGets++; return undefined; } };
void new Proxy({ a: 1 }, countingHandler)[['a'][0]];
assert.strictEqual(handlerGets, 1);

console.log('proxy accessor invariants: ok');
