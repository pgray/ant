const assert = require("node:assert");

function run(flags, lastIndex, input = "aa") {
  const re = new RegExp("a", flags);
  re.lastIndex = lastIndex;
  const m = re.exec(input);
  return [m && m.index, re.lastIndex];
}

assert.deepStrictEqual(run("g", "1"), [1, 2]);
assert.deepStrictEqual(run("g", -1), [0, 1]);
assert.deepStrictEqual(run("y", NaN), [0, 1]);
assert.deepStrictEqual(run("g", 1.7), [1, 2]);
assert.deepStrictEqual(run("g", 5), [null, 0]);
assert.deepStrictEqual(run("y", "x"), [0, 1]);

let calls = 0;
assert.deepStrictEqual(run("g", { valueOf() { calls++; return 1; } }), [1, 2]);
assert.equal(calls, 1);

assert.throws(() => run("g", Symbol()), TypeError);

const t = /a/g;
t.lastIndex = "1";
assert.equal(t.test("ab"), false);
assert.equal(t.lastIndex, 0);

console.log("ok");
