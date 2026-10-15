const assert = require('node:assert');
const { inspect } = require('node:util');

assert.strictEqual(inspect(Object.assign(/a/, { k: 1 })), '/a/ { k: 1 }');
assert.strictEqual(inspect(Object.assign(new Date(0), { k: 1 })), '1970-01-01T00:00:00.000Z { k: 1 }');
assert.strictEqual(inspect(Object.assign(function f() {}, { k: 1 })), '[Function: f] { k: 1 }');
assert.strictEqual(inspect(/a/), '/a/');
assert.strictEqual(inspect(function plain() {}), '[Function: plain]');

const h = function h() {};
h.self = h;
assert.strictEqual(inspect(h).replace(/\s+/g, ' '), '<ref *1> [Function: h] { self: [Circular *1] }');

const holder = {};
holder.f = function g() {};
holder.f.o = holder;
assert.strictEqual(inspect(holder).replace(/\s+/g, ' '), '<ref *1> { f: [Function: g] { o: [Circular *1] } }');

class G { get [Symbol.toStringTag]() { return 'X'; } }
class F { get [Symbol.toStringTag]() { return 'F'; } }
class Thrower { get [Symbol.toStringTag]() { throw new Error('no'); } }
assert.strictEqual(inspect(new G()), 'G [X] {}');
assert.strictEqual(inspect(new F()), 'F {}');
assert.strictEqual(inspect(new Thrower()), 'Thrower {}');
assert.strictEqual(inspect(Object.create(null, { [Symbol.toStringTag]: { value: 'N' } })), '[Object: null prototype] [N] {}');
assert.strictEqual(inspect(Math), 'Object [Math] {}');
assert.strictEqual(inspect(JSON), 'Object [JSON] {}');
assert.strictEqual(inspect(new WebAssembly.Memory({ initial: 1 })), 'Memory [WebAssembly.Memory] {}');
assert.ok(inspect({ a: 1, [Symbol.toStringTag]: 'T' }).includes("Symbol(Symbol.toStringTag)"));

console.log('inspect extra props OK');
