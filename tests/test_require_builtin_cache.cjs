const assert = require('node:assert/strict');
const Module = require('node:module');

const fs = require('node:fs');
for (let i = 0; i < 50; i++) {
  assert.equal(require('node:fs'), fs);
  assert.equal(require('fs'), fs);
  assert.equal(require('f' + 's'), fs);
}

const path = require('path');
for (let i = 0; i < 50; i++) assert.equal(require('node:path'), path);

const dc = require('node:diagnostics_channel');
for (let i = 0; i < 20; i++) assert.equal(require('diagnostics_channel'), dc);
assert.equal(module.children.length, 0);

const fake = { fake: true };
require.cache.fs = { exports: fake };
assert.equal(require('fs'), fake);
assert.equal(require('node:fs'), fs);
delete require.cache.fs;
assert.equal(require('fs'), fs);

require.cache['node:fs'] = { exports: fake };
assert.equal(require('node:fs'), fs);
delete require.cache['node:fs'];

const original = Module._cache;
Module._cache = Object.assign(Object.create(null), original, { fs: { exports: fake } });
assert.equal(require('fs'), fake);
Module._cache = original;
assert.equal(require('fs'), fs);

const proto = { path: { exports: fake } };
Module._cache = Object.assign(Object.create(proto), original);
assert.equal(require('path'), fake);
Module._cache = original;
assert.equal(require('path'), path);

for (let i = 0; i < 2000; i++) ({ [`k${i}`]: [i] });
globalThis.gc?.();
assert.equal(require('node:fs'), fs);
assert.equal(typeof require('node:fs').readFileSync, 'function');

if (typeof Module.registerHooks === 'function') {
  const hooks = Module.registerHooks({
    resolve(specifier, context, next) {
      if (specifier === 'node:fs') return next('node:path', context);
      return next(specifier, context);
    },
  });
  assert.equal(require('node:fs'), path);
  hooks.deregister();
  assert.equal(require('node:fs'), fs);
}

console.log('ok');
