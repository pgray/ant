const assert = require('node:assert');

const nested = {
  'function': 'function () { return "a" in o; }',
  'async function': 'async function () { return "a" in o; }',
  'generator': 'function* () { return "a" in o; }',
  'async generator': 'async function* () { return "a" in o; }',
  'arrow block': '() => { return "a" in o; }',
  'async arrow block': 'async () => { return "a" in o; }',
  'default parameter': 'function (x = "a" in o) { return x; }',
  'arrow default parameter': '(x = "a" in o) => x',
  'array literal': '["a" in o]',
  'array spread': '[...["a" in o]]',
  'object value': '({ x: "a" in o })',
  'computed key': '({ ["a" in o]: 1 })',
  'method': '({ m() { return "a" in o; } })',
  'getter': '({ get g() { return "a" in o; } })',
  'setter': '({ set s(v) { "a" in o; } })',
  'object spread': '({ ...{ x: "a" in o } })',
  'class': 'class { x = "a" in o; static y = "a" in o; m() { return "a" in o; } }',
  'class computed key': 'class { ["a" in o]() {} }',
  'call argument': 'f("a" in o)',
  'call spread argument': 'f(...["a" in o])',
  'new argument': 'new Boolean("a" in o)',
  'tagged template': 'f`${"a" in o}`',
  'async arrow parameter': 'async (x = "a" in o) => x',
  'optional call argument': 'f?.("a" in o)',
  'bracket access': 'o["a" in o]',
  'optional bracket access': 'o?.["a" in o]',
  'template substitution': '`${"a" in o}`',
  'ternary consequent': 'true ? "a" in o : 0',
  'parenthesized': '("a" in o)',
};

for (const [label, expr] of Object.entries(nested)) {
  let fn;
  assert.doesNotThrow(() => { fn = new Function('o', 'f', `for (var v = ${expr}; ;) { return v; }`); }, label);
  assert.doesNotThrow(() => fn({ a: 1 }, x => x), label);
}

for (const src of [
  'for (var [a = "a" in o] = []; ;) return a;',
  'for (let { a = "a" in o } = {}; ;) return a;',
  'for (const { ["a" in o]: a } = { true: true }; ;) return a;',
]) {
  assert.strictEqual(new Function('o', src)({ a: 1 }), true, src);
}

assert.strictEqual(new Function('o', 'for (var v = () => { return "a" in o; }; ;) return v();')({ a: 1 }), true);
assert.strictEqual(new Function('o', 'for (var v = ["a" in o]; ;) return v[0];')({}), false);
assert.strictEqual(new Function('o', 'for (var v = true ? "a" in o : 0; ;) return v;')({ a: 1 }), true);

for (const src of [
  'for (var f = () => "a" in o; ;) {}',
  'for (var v = true ? false : "a" in o; ;) {}',
  'for (var v = "a" in o; ;) {}',
  'for (let v = "a" in o; ;) {}',
]) {
  assert.throws(() => new Function('o', src), SyntaxError, src);
}

const seen = [];
new Function('o', 'seen', 'for (var k = function () { for (;;) { break; } } in o) seen.push(k);')({ a: 1, b: 2 }, seen);
assert.deepStrictEqual(seen, ['a', 'b']);

console.log('for-init nested in ok');
