const assert = require('node:assert');

function child() {
  const sym = Symbol('s');
  const obj = {};
  const fn = () => 1;
  const longKey = 'k'.repeat(70) + 'é';
  const keys = [
    '', 'a', 'http', 'https', 'éß', '😀', longKey, 'k'.repeat(70),
    0, -0, 1, 1.5, NaN, Infinity, -Infinity, 2 ** 53, 10n, -10n, 10n ** 30n,
    true, false, null, undefined, sym, obj, fn, '1', '10',
  ];
  const probes = [
    ...keys,
    'x', 'htt' + 'p', ['ht', 'tp'].join(''), 'é' + 'ß', 'k'.repeat(35) + 'k'.repeat(35) + 'é',
    'k'.repeat(71), -1, 0.5, 11n, BigInt(10), -(10n), {}, Symbol('s'), 'NaN', '-0', 'undefined',
  ];

  const set = new Set(keys);
  const map = new Map(keys.map((k, i) => [k, i]));
  const label = (v) => typeof v === 'symbol' ? v.toString() : typeof v === 'bigint' ? v + 'n' : typeof v === 'object' && v ? 'obj' : typeof v === 'function' ? 'fn' : Object.is(v, -0) ? '-0' : String(v);

  function probeSet(s, v) { return s.has(v); }
  function probeMapHas(m, v) { return m.has(v); }
  function probeMapGet(m, v) { return m.get(v); }

  const out = [];
  let acc = 0;
  for (let round = 0; round < 3000; round++) {
    for (let i = 0; i < probes.length; i++) {
      const v = probes[i];
      const a = probeSet(set, v), b = probeMapHas(map, v), c = probeMapGet(map, v);
      acc += (a ? 1 : 0) + (b ? 2 : 0) + (c === undefined ? 0 : c);
      if (round === 2999) out.push(label(v) + ':' + a + '/' + b + '/' + c);
    }
  }
  out.push('acc ' + acc);

  let built = '';
  for (let i = 0; i < 40; i++) built += 'k';
  for (let i = 0; i < 30; i++) built += 'k';
  out.push('rope ' + set.has(built) + ' ' + map.get(built + 'é') + ' ' + set.has(built + 'é'));

  set.delete('http');
  map.delete(10n);
  map.set(-0, 'zero');
  out.push('mutated ' + probeSet(set, 'http') + ' ' + probeMapGet(map, 10n) + ' ' + probeMapGet(map, 0) + ' ' + probeSet(set, 'https'));

  const errors = [];
  for (const [f, recv] of [[probeSet, map], [probeMapHas, set], [probeMapGet, set], [probeSet, {}], [probeMapGet, null]]) {
    try { f(recv, 'a'); errors.push('none'); } catch (e) { errors.push(e.constructor.name); }
  }
  out.push('errors ' + errors.join(','));

  class MySet extends Set { has(v) { return v === 'forced' || super.has(v); } }
  const mine = new MySet(['a']);
  out.push('subclass ' + probeSet(mine, 'forced') + ' ' + probeSet(mine, 'a') + ' ' + probeSet(mine, 'b'));

  const patched = new Map([['a', 1]]);
  patched.get = function (k) { return 'patched:' + k; };
  out.push('patched ' + probeMapGet(patched, 'a') + ' ' + probeMapGet(map, 'a'));

  const original = Set.prototype.has;
  Set.prototype.has = function (v) { return 'proto:' + v; };
  out.push('proto ' + probeSet(set, 'a'));
  Set.prototype.has = original;
  out.push('restored ' + probeSet(set, 'a'));

  let sideEffects = 0;
  const watched = { get size() { sideEffects++; return 1; } };
  function cached(m, o, v) { const before = o.size; const hit = m.has(v); return before + o.size + (hit ? 1 : 0); }
  for (let i = 0; i < 2000; i++) cached(map, watched, 'a');
  out.push('reads ' + sideEffects);

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
    timeout: 120000,
  });
  const interp = run({}, ['--jitless']);
  assert.strictEqual(interp.status, 0, interp.stdout + interp.stderr);
  for (const [name, debug] of [['default', ''], ['force', 'opt:force,verify'], ['off', 'opt:off']]) {
    const child = run({ ANT_DEBUG: debug });
    assert.strictEqual(child.status, 0, `${name}: ${child.error || child.stdout + child.stderr.slice(-4000)}`);
    assert.strictEqual(child.stdout, interp.stdout, `${name}: output differs from --jitless`);
    console.log(`PASS collection lookup keys ${name}`);
  }
}
