const assert = require('node:assert');

function child() {
  const ascii = ['alpha-beta-gamma-delta-', 'x'.repeat(40), 'host0.example.com/path/'];
  const wide = ['é☃'.repeat(12), '😀x'.repeat(10), 'abc\ud800'.repeat(8)];
  const parts = [...ascii, ...wide, ''];
  const units = (s) => Buffer.byteLength(s, 'utf16le') / 2;

  function pair(a, b) { return a + b; }
  function withConstant(a) { return a + 'éèêëéèêëéèêëéèêëéèêëéèêëéèêëéèêë'; }
  function asciiConstant(a) { return 'scheme://user@host.example.com:8080' + a; }
  function chain(a, b, c) { return a + b + c + a; }
  function grow(base, add, n) { let s = base; for (let i = 0; i < n; i++) s += add; return s; }
  function withNumber(a, i) { return a + (i & 7) + a; }

  const out = [];
  let mismatches = 0;
  const check = (s) => {
    const got = s.length;
    if (got !== units(s)) mismatches++;
    out.push(got);
  };
  for (let round = 0; round < 4000; round++) {
    for (let i = 0; i < parts.length; i++) {
      const a = parts[i], b = parts[(i + round) % parts.length], c = parts[(i * 3 + round) % parts.length];
      const p = pair(a, b);
      check(p);
      check(pair(p, c));
      check(pair(c, p));
      check(withConstant(a));
      check(asciiConstant(a));
      check(chain(a, b, c));
      check(withNumber(a, round));
      if ((round & 63) === 0) check(grow(a, b, 9));
      if ((round & 255) === 0) {
        const flat = p.slice(0);
        check(pair(flat, p));
        check(pair(p.charAt(0) + flat, c));
      }
    }
  }
  console.log('mismatches ' + mismatches + ' sum ' + out.reduce((x, y) => x + y, 0) + ' count ' + out.length);
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
  assert.match(interp.stdout, /^mismatches 0 /);
  for (const [name, debug] of [['default', ''], ['force', 'opt:force,verify'], ['off', 'opt:off']]) {
    const child = run({ ANT_DEBUG: debug });
    assert.strictEqual(child.status, 0, `${name}: ${child.error || child.stdout + child.stderr.slice(-4000)}`);
    assert.strictEqual(child.stdout, interp.stdout, `${name}: output differs from --jitless`);
    console.log(`PASS rope ascii length ${name}`);
  }
}
