const assert = require('node:assert/strict');

const coercible = { valueOf() { return 7; } };
const arr = [1, 1, 5];

function afterForOf(x, items) {
  let s = 0;
  try {
    for (const v of items) {
      if (v > 1) break;
      s += v;
    }
    s = s + x;
    throw new Error('boom ' + s);
  } catch (e) {
    return 'caught:' + e.message;
  }
}

for (let i = 0; i < 3000; i++) assert.equal(afterForOf(i, arr), 'caught:boom ' + (i + 2));
assert.equal(afterForOf(coercible, arr), 'caught:boom 9');

function nested(x, items, mode) {
  let s = 0;
  try {
    try {
      for (const v of items) {
        if (v === 3) continue;
        if (v > 4) break;
        s += v;
      }
      for (let i = 0; i < 3; i++) {
        if (i === 1) continue;
        s += i;
      }
      s = s + x;
      if (mode === 1) throw new Error('inner ' + s);
    } catch (e) {
      if (mode === 1) throw new Error('rethrown ' + e.message);
      return 'unexpected';
    }
    if (mode === 2) throw new Error('outer ' + s);
    return 'done ' + s;
  } catch (e) {
    return 'outer:' + e.message;
  }
}

const items = [1, 3, 2, 9];
for (let i = 0; i < 3000; i++) {
  const mode = i % 3;
  const s = 1 + 2 + 0 + 2 + i;
  const expected = mode === 0 ? 'done ' + s : mode === 1 ? 'outer:rethrown inner ' + s : 'outer:outer ' + s;
  assert.equal(nested(i, items, mode), expected);
}
assert.equal(nested(coercible, items, 1), 'outer:rethrown inner 12');
assert.equal(nested(coercible, items, 2), 'outer:outer 12');
assert.equal(nested(coercible, items, 0), 'done 12');

console.log('ok');
