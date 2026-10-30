const assert = require('node:assert');

function child() {
  const out = [];
  const N = 6000;
  const keys = ['alpha', 'beta', 'gamma', 'delta'];
  const sliced = 'alpha=1&beta=2&gamma=3&delta=4';
  const keyAt = (i) => {
    const k = keys[i & 3];
    const at = sliced.indexOf(k);
    return sliced.slice(at, at + k.length);
  };

  function fill(o, n) {
    for (let i = 0; i < n; i++) o[keyAt(i)] = i;
    return o;
  }
  function read(o, n) {
    let s = '';
    for (let i = 0; i < n; i++) s += String(o[keyAt(i)]) + ',';
    return s;
  }

  let acc = 0;
  for (let r = 0; r < N; r++) {
    const a = fill({ __proto__: null }, 4);
    const b = fill(Object.create(null), 4);
    const c = fill({}, 4);
    acc += a.alpha + b.delta + c.gamma + Object.keys(a).length;
    acc += read(c, 4).length;
  }
  out.push('basic ' + acc);

  const seen = [];
  const proto = {};
  Object.defineProperty(proto, 'beta', { set(v) { seen.push(v); }, get() { return 'from-getter'; }, configurable: true });
  const withSetter = Object.create(proto);
  for (let i = 0; i < 8; i++) withSetter[keyAt(i)] = i;
  out.push('setter ' + seen.join('|') + ' ' + Object.keys(withSetter).join('|') + ' ' + withSetter[keyAt(1)]);

  const frozen = Object.freeze({ alpha: 1 });
  const sealed = Object.seal({ alpha: 1 });
  const closed = Object.preventExtensions({ alpha: 1 });
  const ro = {};
  Object.defineProperty(ro, 'alpha', { value: 1, writable: false, enumerable: true });
  for (let i = 0; i < 8; i++) {
    frozen[keyAt(i)] = 9;
    sealed[keyAt(i)] = 9;
    closed[keyAt(i)] = 9;
    ro[keyAt(i)] = 9;
  }
  out.push('locked ' + JSON.stringify([frozen, sealed, closed, ro]));

  const strictErrors = (function () {
    'use strict';
    let n = 0;
    for (const target of [frozen, sealed, closed, ro]) {
      for (let i = 0; i < 4; i++) {
        try { target[keyAt(i)] = 7; } catch (e) { n += e instanceof TypeError ? 1 : 100; }
      }
    }
    return n;
  })();
  out.push('strict ' + strictErrors);

  const viaProto = {};
  const protoKey = '__proto__x'.slice(0, 9);
  viaProto[protoKey] = { inherited: 1 };
  const nullViaProto = { __proto__: null };
  nullViaProto[protoKey] = 5;
  out.push('proto-key ' + viaProto.inherited + ' ' + Object.keys(viaProto).length + ' ' + nullViaProto.__proto__ + ' ' + Object.keys(nullViaProto).join());

  const numeric = {};
  for (const k of ['0', '12', '-1', '1e3', 'Infinity', 'NaN', '01']) numeric[k] = k.length;
  out.push('numeric ' + JSON.stringify(numeric) + ' ' + Object.keys(numeric).join(','));

  const arr = [1, 2, 3];
  arr['len'.slice(0, 3)] = 'x';
  arr['length'.slice(0)] = 2;
  out.push('array ' + JSON.stringify(arr) + ' ' + arr.length + ' ' + arr.len);

  const buf = Buffer.from([1, 2, 3]);
  buf['Infinity'.slice(0)] = 5;
  buf['tag'.slice(0)] = 'b';
  out.push('buffer ' + buf['Infinity'] + ' ' + buf.tag + ' ' + buf.length);

  const trapped = [];
  const px = new Proxy({}, { set(t, k, v) { trapped.push('set:' + k); t[k] = v; return true; }, get(t, k) { trapped.push('get:' + String(k)); return t[k]; } });
  for (let i = 0; i < 4; i++) { px[keyAt(i)] = i; px[keyAt(i)]; }
  out.push('proxy ' + trapped.join(','));

  const shared = { base: 1 };
  const child1 = Object.create(shared);
  let misses = '';
  for (let i = 0; i < 3000; i++) misses += child1['later'.slice(0)] === undefined ? '' : 'x';
  shared.later = 'now';
  out.push('late-proto ' + misses.length + ' ' + child1['later'.slice(0)] + ' ' + child1['base'.slice(0)]);

  const toStr = {}['toString'.slice(0)] === Object.prototype.toString;
  const nullToStr = { __proto__: null }['toString'.slice(0)];
  out.push('inherited ' + toStr + ' ' + nullToStr);

  const re = { alpha: 1, beta: 2 };
  for (let i = 0; i < 3000; i++) re[keyAt(i)] = i;
  Object.defineProperty(re, 'alpha', { writable: false });
  re[keyAt(0)] = 'blocked';
  delete re.beta;
  re[keyAt(1)] = 'again';
  out.push('redefine ' + JSON.stringify(re) + ' ' + Object.keys(re).join(','));

  let s = '';
  s += 'ab'.slice(0, 1);
  let t = '';
  t += 5;
  let u = '';
  u += { toString() { return 'obj'; } };
  let long = '';
  for (let i = 0; i < 400; i++) long += 'xyz'.slice(i % 3, i % 3 + 1);
  out.push('append ' + s + ' ' + t + ' ' + typeof t + ' ' + u + ' ' + long.length + ' ' + long.slice(0, 6));

  const text = 'héllo wörld';
  let chars = '';
  for (let i = 0; i < text.length; i++) chars += text.slice(i, i + 1) + '.';
  out.push('slice ' + chars + ' ' + 'abc'.slice(1, 2) + 'abc'.slice(-1) + 'abc'.slice(2, 1).length);

  console.log(out.join('\n'));
}

if (process.argv[2] === 'child') {
  child();
} else {
  const { spawnSync } = require('node:child_process');
  const base = { ...process.env };
  delete base.ANT_DEBUG;
  const run = (env, args = []) => spawnSync(process.execPath, [...args, __filename, 'child'], {
    encoding: 'utf8',
    env: { ...base, ...env, NO_COLOR: '1' },
    timeout: 120000, maxBuffer: 64 * 1024 * 1024,
  });
  const interp = run({}, ['--jitless']);
  assert.strictEqual(interp.status, 0, interp.stdout + interp.stderr);
  for (const [name, debug] of [['default', ''], ['verify', 'opt:verify'], ['force', 'opt:force,verify'], ['off', 'opt:off']]) {
    const child = run({ ANT_DEBUG: debug });
    assert.strictEqual(child.status, 0, `${name}: ${child.error || child.stdout + child.stderr.slice(-4000)}`);
    assert.strictEqual(child.stdout, interp.stdout, `${name}: output differs from --jitless`);
    console.log(`PASS keyed string access ${name}`);
  }
}
