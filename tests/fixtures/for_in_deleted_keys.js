const o = { a: 1, b: 2, c: 3 };
const seenObject = [];
for (const k in o) { seenObject.push(k); if (k === 'a') delete o.b; }
console.log(JSON.stringify(seenObject));

const arr = [1, 2, 3];
const seenArray = [];
for (const k in arr) { seenArray.push(k); if (k === '0') arr.length = 1; }
console.log(JSON.stringify(seenArray));

const view = new Uint8Array([1, 2, 3]);
const seenView = [];
for (const k in view) { seenView.push(k); if (k === '0') view.buffer.transfer(); }
console.log(JSON.stringify(seenView));

const proto = { inherited: 1 };
const child = Object.create(proto);
child.own = 1;
const seenInherited = [];
for (const k in child) { seenInherited.push(k); if (k === 'own') delete proto.inherited; }
console.log(JSON.stringify(seenInherited));
