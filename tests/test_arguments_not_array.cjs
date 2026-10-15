// Arguments objects share Array storage but are ordinary objects. APIs that
// expect an Array (structuredClone, v8.serialize, JSON replacer lists,
// Buffer.concat, process.hrtime, child_process args, assert deep equality)
// must not treat them as one, and readers must honor a reassigned length.
// Output must match what Node prints.
const assert = require('node:assert');
const { spawnSync } = require('node:child_process');

const body = String.raw`
function A(){ return arguments; }
function S(){ 'use strict'; return arguments; }
function L(){ arguments.length = 1; return arguments; }
function G(){ arguments[5] = 9; arguments.length = 6; return arguments; }
const show=r=>{ if (r===undefined) return 'undefined'; try { return JSON.stringify(r); } catch { return String(r); } };
const t=(n,fn)=>{ try { console.log(n, show(fn())); } catch(e){ console.log(n,'throws',e.name, e.code||''); } };
const cp=require('child_process'), util=require('util'), assert=require('assert'), v8=require('v8');
for (const [n,mk] of [['sloppy',A],['strict',S]]) {
  t(n+' isArray', ()=>Array.isArray(mk(1)));
  t(n+' toString', ()=>Object.prototype.toString.call(mk(1)));
  t(n+' json', ()=>JSON.stringify(mk(1,2)));
  t(n+' keys', ()=>Object.keys(mk(1,2)));
  t(n+' names', ()=>Object.getOwnPropertyNames(mk(1,2)));
  t(n+' concat', ()=>[].concat(mk(1)));
  t(n+' spread obj', ()=>({...mk(1,2)}));
  t(n+' inspect', ()=>util.inspect(mk(1,'a')));
  t(n+' inspect empty', ()=>util.inspect(mk()));
}
t('structuredClone', ()=>structuredClone(A(1,2)));
t('structuredClone nested', ()=>structuredClone({x:A(1)}));
t('v8 serialize', ()=>v8.serialize(A(1,2)));
t('v8 serialize fn', ()=>v8.serialize(()=>1));
t('v8 serialize sym', ()=>v8.serialize(Symbol('s')));
t('v8 roundtrip', ()=>v8.deserialize(v8.serialize({a:[1,'x',null]})));
t('deepStrictEqual arr', ()=>assert.deepStrictEqual(A(1,2), [1,2]));
t('deepEqual arr', ()=>assert.deepEqual(A(1,2), [1,2]));
t('deepStrictEqual args', ()=>assert.deepStrictEqual(A(1,2), A(1,2)));
t('JSON replacer args', ()=>JSON.stringify({a:1,b:2}, A('a')));
t('JSON replacer arraylike', ()=>JSON.stringify({a:1,b:2}, {length:1, 0:'a'}));
t('JSON replacer array', ()=>JSON.stringify({a:1,b:2}, ['a']));
t('JSON replacer proxy', ()=>JSON.stringify({a:1,b:2}, new Proxy(['b'], {})));
t('Buffer.concat args', ()=>Buffer.concat(A(Buffer.from('a'))));
t('Buffer.concat obj', ()=>Buffer.concat({length:0}));
t('Buffer.concat array', ()=>Buffer.concat([Buffer.from('a'), Buffer.from('b')]).toString());
t('hrtime args', ()=>process.hrtime(A(0,0)));
t('hrtime len', ()=>process.hrtime([1,2,3]));
t('hrtime ok', ()=>process.hrtime([0,0]).length);
t('execFileSync args', ()=>String(cp.execFileSync('echo', A('x'))).trim());
t('execFileSync array', ()=>String(cp.execFileSync('echo', ['x'])).trim());
t('u8 from modified length', ()=>[...new Uint8Array(L(1,2,3))]);
t('u8 from grown', ()=>[...new Uint8Array(G(1))]);
t('spread modified length', ()=>[...L(1,2,3)]);
t('apply modified length', ()=>Math.max.apply(null, L(1,5,9)));
t('Array.from modified', ()=>Array.from(G(1)));
`;

const env = { ...process.env, NO_COLOR: '1' };
delete env.FORCE_COLOR;
const child = spawnSync(process.execPath, ['-e', body], { encoding: 'utf8', timeout: 60000, env });
assert.strictEqual(child.status, 0, child.stderr);
assert.strictEqual(child.stdout, "sloppy isArray false\nsloppy toString \"[object Arguments]\"\nsloppy json \"{\\\"0\\\":1,\\\"1\\\":2}\"\nsloppy keys [\"0\",\"1\"]\nsloppy names [\"0\",\"1\",\"length\",\"callee\"]\nsloppy concat [{\"0\":1}]\nsloppy spread obj {\"0\":1,\"1\":2}\nsloppy inspect \"[Arguments] { '0': 1, '1': 'a' }\"\nsloppy inspect empty \"[Arguments] {}\"\nstrict isArray false\nstrict toString \"[object Arguments]\"\nstrict json \"{\\\"0\\\":1,\\\"1\\\":2}\"\nstrict keys [\"0\",\"1\"]\nstrict names [\"0\",\"1\",\"length\",\"callee\"]\nstrict concat [{\"0\":1}]\nstrict spread obj {\"0\":1,\"1\":2}\nstrict inspect \"[Arguments] { '0': 1, '1': 'a' }\"\nstrict inspect empty \"[Arguments] {}\"\nstructuredClone throws DataCloneError 25\nstructuredClone nested throws DataCloneError 25\nv8 serialize throws Error \nv8 serialize fn throws Error \nv8 serialize sym throws Error \nv8 roundtrip {\"a\":[1,\"x\",null]}\ndeepStrictEqual arr throws AssertionError ERR_ASSERTION\ndeepEqual arr throws AssertionError ERR_ASSERTION\ndeepStrictEqual args undefined\nJSON replacer args \"{\\\"a\\\":1,\\\"b\\\":2}\"\nJSON replacer arraylike \"{\\\"a\\\":1,\\\"b\\\":2}\"\nJSON replacer array \"{\\\"a\\\":1}\"\nJSON replacer proxy \"{\\\"b\\\":2}\"\nBuffer.concat args throws TypeError ERR_INVALID_ARG_TYPE\nBuffer.concat obj throws TypeError ERR_INVALID_ARG_TYPE\nBuffer.concat array \"ab\"\nhrtime args throws TypeError ERR_INVALID_ARG_TYPE\nhrtime len throws RangeError ERR_OUT_OF_RANGE\nhrtime ok 2\nexecFileSync args \"\"\nexecFileSync array \"x\"\nu8 from modified length [1]\nu8 from grown [1,0,0,0,0,9]\nspread modified length [1]\napply modified length 1\nArray.from modified [1,null,null,null,null,9]\n");
console.log('PASS arguments objects are not treated as arrays');
