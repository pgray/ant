const assert = require('node:assert');
const v8 = require('node:v8');

let deep = { leaf: true };
for (let i = 0; i < 200; i++) deep = { next: deep, list: [i] };

let back = v8.deserialize(v8.serialize(deep));
let levels = 0;
while (back.next) {
  back = back.next;
  levels++;
}
assert.strictEqual(levels, 200);
assert.strictEqual(back.leaf, true);

let nested = [];
for (let i = 0; i < 200; i++) nested = [nested];
let arr = v8.deserialize(v8.serialize(nested));
for (let i = 0; i < 200; i++) arr = arr[0];
assert.deepStrictEqual(arr, []);

console.log('v8 serialize depth OK');
