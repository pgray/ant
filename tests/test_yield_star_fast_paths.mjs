import assert from 'node:assert';

function* inner() {
  const got = [];
  try {
    got.push(yield 1);
    got.push(yield 2);
  } finally {
    got.push('inner-finally');
  }
  return got;
}

const outerLog = [];
function* outer() { outerLog.push(yield* inner()); return 'outer-done'; }

const g = outer();
assert.deepStrictEqual(g.next('ignored'), { value: 1, done: false });
assert.deepStrictEqual(g.next('a'), { value: 2, done: false });
assert.deepStrictEqual(g.next('b'), { value: 'outer-done', done: true });
assert.deepStrictEqual(outerLog, [['a', 'b', 'inner-finally']]);

const thrower = outer();
thrower.next();
assert.throws(() => thrower.throw(new Error('boom')), /boom/);
assert.deepStrictEqual(thrower.next(), { value: undefined, done: true });

const returner = outer();
returner.next();
assert.deepStrictEqual(returner.return('early'), { value: 'early', done: true });

function* catcher() { try { yield 1; } catch (e) { yield `caught ${e}`; } }
function* wrapsCatcher() { return yield* catcher(); }
const c = wrapsCatcher();
c.next();
assert.deepStrictEqual(c.throw('x'), { value: 'caught x', done: false });

const patched = inner();
const seen = [];
patched.next = function (v) { seen.push(v); return { value: 'patched', done: seen.length > 1 }; };
assert.deepStrictEqual([...(function* () { yield* patched; })()], ['patched']);
assert.deepStrictEqual(seen, [undefined, undefined]);

function* recurse(n) { if (n > 0) yield* recurse(n - 1); else yield 'bottom'; }
assert.deepStrictEqual([...recurse(200)], ['bottom']);
assert.throws(() => { for (const _ of recurse(1e6)); }, RangeError);

function* reentrant() { yield* self; }
const self = reentrant();
assert.throws(() => self.next(), TypeError);

async function collect(iterable) {
  const out = [];
  for await (const v of iterable) out.push(v);
  return out;
}

assert.deepStrictEqual(await collect((async function* () { yield* [1, 2]; })()), [1, 2]);

const ownAsync = [1, 2];
ownAsync[Symbol.asyncIterator] = async function* () { yield 'own'; };
assert.deepStrictEqual(await collect((async function* () { yield* ownAsync; })()), ['own']);
assert.deepStrictEqual(await collect(ownAsync), ['own']);

class Sub extends Array {}
Sub.prototype[Symbol.asyncIterator] = async function* () { yield 'sub'; };
assert.deepStrictEqual(await collect((async function* () { yield* Sub.from([1]); })()), ['sub']);

Array.prototype[Symbol.asyncIterator] = async function* () { yield 'proto'; };
assert.deepStrictEqual(await collect((async function* () { yield* [9]; })()), ['proto']);
assert.deepStrictEqual(await collect([9]), ['proto']);
delete Array.prototype[Symbol.asyncIterator];
assert.deepStrictEqual(await collect((async function* () { yield* [9]; })()), [9]);

assert.strictEqual(await Promise.resolve({ value: 1, done: false }).then((r) => r.value), 1);
const ownThen = await new Promise((resolve) => resolve({ then(ok) { ok('own-then'); } }));
assert.strictEqual(ownThen, 'own-then');

Object.prototype.then = function (ok) { delete Object.prototype.then; ok('proto-then'); };
assert.strictEqual(await new Promise((resolve) => resolve({})), 'proto-then');

Object.defineProperty(Object.prototype, 'then', {
  configurable: true,
  get() { delete Object.prototype.then; return (ok) => ok('getter-then'); },
});
assert.strictEqual(await new Promise((resolve) => resolve({ plain: true })), 'getter-then');

console.log('yield* fast paths: ok');
