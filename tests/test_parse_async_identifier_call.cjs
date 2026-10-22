const assert = require('node:assert');

function run(src) {
  return new Function(src)();
}

const def = 'function async(...a) { return a.length } ';

assert.strictEqual(run(def + 'return async()'), 0);
assert.strictEqual(run(def + 'return async(1, 2)'), 2);
assert.strictEqual(run(def + 'return async(1,)'), 1);
assert.strictEqual(run(def + 'return async(1, 2,)'), 2);
assert.strictEqual(run(def + 'return async(...[1, 2, 3])'), 3);
assert.strictEqual(run(def + 'return async\n()'), 0);
assert.strictEqual(run('function async() { return () => 7 } return async()()'), 7);
assert.strictEqual(run('function async() { this.v = 9 } return new async().v'), 9);

assert.strictEqual(run('return (async () => 1).constructor.name'), 'AsyncFunction');
assert.strictEqual(run('return (async (a, b,) => a + b).constructor.name'), 'AsyncFunction');
assert.strictEqual(run('return (async x => x).constructor.name'), 'AsyncFunction');
assert.strictEqual(run('return (async (...r) => r.length).constructor.name'), 'AsyncFunction');

(async () => {
  function async() {
    return 5;
  }
  assert.strictEqual(await async(), 5);
  assert.strictEqual(await (async (a, b,) => a + b)(1, 2), 3);
  console.log('ok');
})();
