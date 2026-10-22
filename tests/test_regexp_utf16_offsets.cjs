const assert = require('node:assert');

function execAt(source, flags, input, lastIndex) {
  const re = new RegExp(source, flags);
  re.lastIndex = lastIndex;
  const m = re.exec(input);
  return [m && m[0], m && m.index, re.lastIndex, m && m.indices ? [...m.indices[0]] : null];
}

for (const flags of ['g', 'gu', 'dg', 'dgu']) {
  assert.deepStrictEqual(execAt('l', flags, 'ël', 0), ['l', 1, 2, flags.includes('d') ? [1, 2] : null], flags);
  assert.deepStrictEqual(execAt('b', flags, '😀ab', 0), ['b', 3, 4, flags.includes('d') ? [3, 4] : null], flags);
  assert.deepStrictEqual(execAt('\\S+', flags, 'ëlet J=1', 4), ['J=1', 5, 8, flags.includes('d') ? [5, 8] : null], flags);
}

for (const flags of ['y', 'yu', 'dy']) {
  assert.deepStrictEqual(execAt('l', flags, 'ël', 1), ['l', 1, 2, flags.includes('d') ? [1, 2] : null], flags);
  assert.deepStrictEqual(execAt('a', flags, '😀a', 2), ['a', 2, 3, flags.includes('d') ? [2, 3] : null], flags);
  assert.deepStrictEqual(execAt('l', flags, 'ël', 0), [null, null, 0, null], flags);
}

assert.deepStrictEqual([.../l/d.exec('ëlet').indices[0]], [1, 2]);
assert.deepStrictEqual([.../(?<w>e)(t)/d.exec('ëlet').indices.groups.w], [2, 3]);
assert.strictEqual(/b/.exec('😀b').index, 2);
assert.strictEqual(/b/u.exec('é😀b').index, 3);

{
  const re = /t/g;
  assert.strictEqual(re.test('ëtt'), true);
  assert.strictEqual(re.lastIndex, 2);
  assert.strictEqual(re.test('ëtt'), true);
  assert.strictEqual(re.lastIndex, 3);
  assert.strictEqual(re.test('ëtt'), false);
  assert.strictEqual(re.lastIndex, 0);
}

{
  const re = /x/y;
  re.lastIndex = 1;
  assert.strictEqual(re.exec('éab'), null);
  assert.strictEqual(re.lastIndex, 0);
}

{
  const re = /a/g;
  Object.defineProperty(re, 'lastIndex', { writable: false, value: 0 });
  assert.throws(() => re.exec('b'), TypeError);
  assert.throws(() => re.exec('éa'), TypeError);
}

assert.deepStrictEqual([...'éaéa'.matchAll(/a/g)].map(m => m.index), [1, 3]);
assert.deepStrictEqual([...'😀a😀a'.matchAll(/a/gu)].map(m => m.index), [2, 5]);

{
  const positions = [];
  'ëaéa😀a'.replace(/a/g, (m, i) => { positions.push(i); return m; });
  assert.deepStrictEqual(positions, [1, 3, 6]);
  assert.strictEqual('ëaéa'.replace(/a/g, '[$`]'), 'ë[ë]é[ëaé]');
}

assert.deepStrictEqual('ë,a,é'.split(/,/), ['ë', 'a', 'é']);
assert.deepStrictEqual('😀,ë'.split(/,/u), ['😀', 'ë']);

{
  class Species extends RegExp {
    exec(s) {
      this.calls = (this.calls || 0) + 1;
      return super.exec(s);
    }
  }
  assert.deepStrictEqual('é-a-ë'.split(new Species('-')), ['é', 'a', 'ë']);
  assert.deepStrictEqual('é😀-ë'.split(new Species('-', 'u')), ['é😀', 'ë']);
}

assert.deepStrictEqual([...'😀😀'.matchAll(/(?:)/gu)].map(m => m.index), [0, 2, 4]);
assert.deepStrictEqual([...'éa'.matchAll(/(?:)/g)].map(m => m.index), [0, 1, 2]);
assert.strictEqual('😀'.replace(/(?:)/gu, '-'), '-😀-');
assert.strictEqual('é'.replace(/(?:)/g, '-'), '-é-');
assert.deepStrictEqual('😀a'.split(/(?:)/u), ['😀', 'a']);
assert.deepStrictEqual('éa'.split(/(?:)/), ['é', 'a']);

console.log('regexp utf16 offsets ok');
