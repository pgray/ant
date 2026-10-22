const assert = require("node:assert");

assert.equal("abcb".replace(/(?<m>b)/g, "[$<m>]"), "a[b]c[b]");
assert.equal("abc".replace(/(?<m>b)/, "[$<m>|$<x>|$<m]"), "a[b||$<m]c");
assert.equal("abc".replace(/(b)/, "[$<m>]"), "a[$<m>]c");
assert.equal("ac".replace(/a(?<m>b)?/, "[$<m>]"), "[]c");
assert.equal("abcb".replaceAll(/(?<m>b)/g, "$<m>$<m>"), "abbcbb");
assert.equal("abc".replace("b", "[$<m>]"), "a[$<m>]c");

const custom = /b/;
custom.exec = function () {
  const m = ["b"];
  m.index = 1;
  m.groups = { m: "Z", get n() { return 7; } };
  return m;
};
assert.equal("abc".replace(custom, "[$<m>$<n>$<q>]"), "a[Z7]c");

const throwing = /b/;
throwing.exec = function () {
  const m = ["b"];
  m.index = 1;
  m.groups = { get m() { throw new Error("groups getter"); } };
  return m;
};
assert.throws(() => "abc".replace(throwing, "[$<m>]"), /groups getter/);

let replacerArgs;
"abc".replace(/(?<m>b)/, (...a) => { replacerArgs = a; return ""; });
assert.deepStrictEqual(replacerArgs, ["b", "b", 1, "abc", Object.assign(Object.create(null), { m: "b" })]);

"abc".replace(/(b)/, (...a) => { replacerArgs = a; return ""; });
assert.deepStrictEqual(replacerArgs, ["b", "b", 1, "abc"]);

const seen = [];
"abab".replaceAll(/(?<m>b)/g, (...a) => { seen.push(a.length, a.at(-1).m); return ""; });
assert.deepStrictEqual(seen, [5, "b", 5, "b"]);

let warmed;
for (let i = 0; i < 3000; i++) warmed = ("a" + i + "b").replace(/(?<d>\d+)/, "<$<d>>");
assert.equal(warmed, "a<2999>b");

console.log("ok");
