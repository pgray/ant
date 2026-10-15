// Typed arrays expose their indices as own enumerable properties (keys,
// entries, for-in, in, hasOwn, descriptors, JSON, spread); buffer and
// BYTES_PER_ELEMENT come from the prototype, and subarray shares the buffer
// object. Output must match what Node prints.
const assert = require('node:assert');
const { spawnSync } = require('node:child_process');

const body = String.raw`
const s=v=>JSON.stringify(v);
const u=new Uint8Array([4,5]); u.foo=1;
console.log(s(Object.keys(u)), s(Object.entries(u)), s(Object.values(u)), s({...u}), s(Object.assign({}, u)), s(u));
const r=[]; for (const k in u) r.push(k); console.log(r.join());
console.log(u.buffer instanceof ArrayBuffer, u.buffer.byteLength, u.BYTES_PER_ELEMENT, Object.hasOwn(u,'buffer'), Object.hasOwn(u, 0), Object.hasOwn(u, 2), 0 in u, 2 in u, '-0' in u, s(Object.getOwnPropertyDescriptor(u, '1')), s(Object.getOwnPropertyDescriptor(u, '2')));
const dv=new DataView(new ArrayBuffer(4), 1); console.log(dv.byteLength, dv.byteOffset, dv.buffer.byteLength, s(Object.getOwnPropertyNames(dv)));
const ab=new ArrayBuffer(8); console.log(s(Object.getOwnPropertyNames(ab)), ab.byteLength); const t2=ab.transfer(); console.log(ab.byteLength, t2.byteLength, ab.detached);
const sub=u.subarray(1); console.log(sub.buffer===u.buffer, sub.byteOffset, s(Object.keys(sub)), u.slice(1).buffer===u.buffer);
console.log(s(Object.keys(Buffer.from('hey'))), Buffer.from('hey').buffer instanceof ArrayBuffer);
class X extends Uint8Array {}; const x=new X(2); console.log(s(Object.keys(x)), x.buffer.byteLength, x.subarray(1).buffer === x.buffer);
const big=new BigInt64Array([1n, 2n]); console.log(s(Object.keys(big)), Object.values(big).map(String).join());
try { Object.getOwnPropertyDescriptor(Uint8Array.prototype, 'buffer').get.call({}); } catch (e) { console.log(e.name); }
`;

const env = { ...process.env, NO_COLOR: '1' };
delete env.FORCE_COLOR;
const child = spawnSync(process.execPath, ['-e', body], { encoding: 'utf8', timeout: 60000, env });
assert.strictEqual(child.status, 0, child.stderr);
assert.strictEqual(child.stdout, "[\"0\",\"1\",\"foo\"] [[\"0\",4],[\"1\",5],[\"foo\",1]] [4,5,1] {\"0\":4,\"1\":5,\"foo\":1} {\"0\":4,\"1\":5,\"foo\":1} {\"0\":4,\"1\":5,\"foo\":1}\n0,1,foo\ntrue 2 1 false true false true false false {\"value\":5,\"writable\":true,\"enumerable\":true,\"configurable\":true} undefined\n3 1 4 []\n[] 8\n0 8 true\ntrue 1 [\"0\"] false\n[\"0\",\"1\",\"2\"] true\n[\"0\",\"1\"] 2 true\n[\"0\",\"1\"] 1,2\nTypeError\n");
console.log('PASS typed array own keys match Node');
