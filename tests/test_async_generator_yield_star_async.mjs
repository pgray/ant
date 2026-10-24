import assert from 'node:assert';

async function collect(iterable) {
  const out = [];
  for await (const v of iterable) out.push(v);
  return out;
}

async function* inner(...values) {
  for (const v of values) yield v;
  return 'inner-done';
}

assert.deepStrictEqual(await collect((async function* () { yield* inner(1, 2); })()), [1, 2]);

assert.deepStrictEqual(
  await collect((async function* () {
    for await (const b of [1, 2]) yield* inner(b, b * 10);
  })()),
  [1, 10, 2, 20]
);

const returned = [];
await collect((async function* () { returned.push(yield* inner(1)); })());
assert.deepStrictEqual(returned, ['inner-done']);

const received = [];
const echo = (async function* () {
  received.push(yield 'a');
  received.push(yield 'b');
  return 'echo-done';
})();
const outer = (async function* () { return yield* echo; })();
assert.deepStrictEqual(await outer.next('ignored'), { value: 'a', done: false });
assert.deepStrictEqual(await outer.next('x'), { value: 'b', done: false });
assert.deepStrictEqual(await outer.next('y'), { value: 'echo-done', done: true });
assert.deepStrictEqual(received, ['x', 'y']);

const log = [];
const guarded = (async function* () {
  try { yield 1; yield 2; } catch (err) { log.push('inner caught ' + err.message); yield 'recovered'; }
  finally { log.push('inner finally'); }
})();
const thrower = (async function* () { yield* guarded; })();
assert.deepStrictEqual(await thrower.next(), { value: 1, done: false });
assert.deepStrictEqual(await thrower.throw(new Error('boom')), { value: 'recovered', done: false });
assert.deepStrictEqual(await thrower.return('bye'), { value: 'bye', done: true });
assert.deepStrictEqual(log, ['inner caught boom', 'inner finally']);

const rejecting = (async function* () {
  yield* { [Symbol.asyncIterator]() { return { next: () => Promise.reject(new Error('rejected')) }; } };
})();
await assert.rejects(rejecting.next(), { message: 'rejected' });

let calls = 0;
const manual = {
  [Symbol.asyncIterator]() {
    return {
      next(v) {
        calls++;
        return new Promise((resolve) => setTimeout(() => resolve(calls > 2 ? { value: 'end', done: true } : { value: calls, done: false }), 1));
      },
    };
  },
};
const manualResult = [];
const manualOuter = (async function* () { manualResult.push(yield* manual); })();
assert.deepStrictEqual(await collect(manualOuter), [1, 2]);
assert.deepStrictEqual(manualResult, ['end']);

const noThrow = (async function* () {
  yield* { [Symbol.asyncIterator]() { return { next: async () => ({ value: 1, done: false }), return: async () => ({ done: true }) }; } };
})();
await noThrow.next();
await assert.rejects(noThrow.throw(new Error('x')));

const syncInAsync = (async function* () { yield* [Promise.resolve(6), 7]; })();
assert.deepStrictEqual(await collect(syncInAsync), [6, 7]);

await assert.rejects(collect((async function* () { yield* 5; })()), TypeError);

console.log('async generator yield* async: ok');
