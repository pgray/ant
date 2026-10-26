const assert = require('node:assert');

class Boom { constructor() { throw new Error('boom'); } }

function columnOf(fn) {
  try { fn(); } catch (e) {
    const line = e.stack.split('\n').find(l => l.includes('test_stack_column_backward_utf8') && !l.includes('Boom'));
    return Number(line.match(/:(\d+)\)?$/)[1]);
  }
}

const first = () => { const a = 'ééé', b = '😀😀'; return new Boom(a, b, 'ü'.repeat(3)); };
const second = () => { const c = '€'; const d = '😀'; return [c, d, new Boom(c + d, '日本語')]; };

assert.strictEqual(columnOf(first), 59);
assert.strictEqual(columnOf(second), 69);
console.log('stack column backward utf8 ok');
