const assert = require('node:assert');

const reportedColumn = fn => {
  try { fn(); } catch (e) {
    const frame = e.stack.split('\n').find(l => l.includes(__filename));
    return Number(frame.match(/:(\d+)\)?$/)[1]);
  }
  throw new Error('expected throw');
};

const o = {};
function member() { return 1 + o.missing(1, 2); }
function plain() { let q; return q(3, 4) + 1; }

const fileLines = require('node:fs').readFileSync(__filename, 'utf8').split('\n');
const expectColumn = (fn, needle) => {
  const line = fileLines.find(l => l.startsWith(`function ${fn.name}(`));
  assert.strictEqual(reportedColumn(fn), line.indexOf(needle) + 1, fn.name);
};

expectColumn(member, 'missing(');
expectColumn(plain, 'q(3');

const falsy = f => { try { f(); } catch (e) { return e.message; } };
assert.strictEqual(falsy(() => assert(false)), 'The expression evaluated to a falsy value:\n\n  assert(false)\n');
assert.strictEqual(falsy(() => assert.ok(1 > 2)), 'The expression evaluated to a falsy value:\n\n  assert.ok(1 > 2)\n');
assert.strictEqual(falsy(() => { return assert.ok(0 && 'x'); }), "The expression evaluated to a falsy value:\n\n  assert.ok(0 && 'x')\n");

console.log('call site positions OK');
