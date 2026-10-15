// Object spread and rest follow CopyDataProperties: array, arguments, string,
// String-wrapper and typed-array elements are copied, keys are defined (no
// setters, __proto__ stays an own key), getter errors propagate, and
// Object.assign handles string sources. Output must match what Node prints.
const assert = require('node:assert');
const { spawnSync } = require('node:child_process');

const body = String.raw`
const s=v=>JSON.stringify(v);
function A(){ return arguments; } function S(){ 'use strict'; return arguments; }
console.log('arr', s({...[1,,3]}), 'args', s({...A(1,2)}), s({...S(1,2)}));
console.log('str', s({...'ab'}), s({...new String('xy')}), 'num', s({...5}), s({...true}), s({...null}), s({...undefined}));
const o=JSON.parse('{"__proto__":{"x":1},"a":2}'); const sp={...o};
console.log('proto', Object.getPrototypeOf(sp)===Object.prototype, Object.keys(sp).join(), sp.x);
let hit=false; Object.defineProperty(Object.prototype,'zz',{set(v){hit=true}, configurable:true});
const s2={...{zz:1}}; console.log('setter', hit, Object.keys(s2).join()); delete Object.prototype.zz;
console.log('order', Object.keys({...{b:1, 2:2, a:3, 1:4, [Symbol.for('q')]:5}}).join(), Object.getOwnPropertySymbols({...{[Symbol.for('q')]:5}}).length);
const g={get a(){throw new Error('boom')}}; try { ({...g}); console.log('nothrow'); } catch(e) { console.log('threw', e.message); }
try { const {...r} = g; console.log('nothrow'); } catch(e) { console.log('rest threw', e.message); }
const g2={get v(){ return 42; }}; const c2={...g2}; console.log('getter', s(Object.getOwnPropertyDescriptor(c2,'v')));
console.log('override', s({a:1, ...{a:2}, b:3}), s({...{a:1}, a:9}));
console.log('accessor-target', ({get a(){return 1}, ...{a:2}}).a, s(Object.getOwnPropertyDescriptor({get a(){return 1}, ...{a:2}}, 'a')));
console.log('nonenum', s({...Object.defineProperty({}, 'h', {value:1})}));
const {a, ...rest} = {a:1, b:2, [Symbol.iterator]: 3}; console.log('rest', s(rest), Object.getOwnPropertySymbols(rest).length);
const {0:z, ...r2} = [5,6,7]; console.log('arr rest', s(r2));
const {x, ...r3} = 'hey'; console.log('str rest', s(r3));
console.log('proxy', s({...new Proxy({p:1,q:2},{})}));
console.log('fn', s({...Object.assign(function(){}, {k:1})}));
console.log('map', s({...new Map([[1,2]])}), 'date', s({...Object.assign(new Date(0),{d:1})}));
console.log('typed', s({...new Uint8Array([4,5])}));
console.log('builtin', ({...{f: Math.max}}).f === Math.max, ({...{f: Math.max}}).f.name);
console.log('assign', s(Object.assign({}, 'ab', null, 1)), typeof Object.assign('x'), s(Object.assign({}, new String('xy'))));
let hit2=false; const t={set q(v){hit2=true}}; Object.assign(t,{q:1}); console.log('assign setter', hit2);
console.log('assign args', s(Object.assign({}, A(1,2))));
try { Object.assign({}, g); } catch (e) { console.log('assign threw', e.message); }
function hot(o) { return {...o, z: 1}; } let acc = 0; for (let i = 0; i < 2000; i++) acc += hot({a: i}).a; console.log('hot', acc);
`;

const env = { ...process.env, NO_COLOR: '1' };
delete env.FORCE_COLOR;
const child = spawnSync(process.execPath, ['-e', body], { encoding: 'utf8', timeout: 60000, env });
assert.strictEqual(child.status, 0, child.stderr);
assert.strictEqual(child.stdout, "arr {\"0\":1,\"2\":3} args {\"0\":1,\"1\":2} {\"0\":1,\"1\":2}\nstr {\"0\":\"a\",\"1\":\"b\"} {\"0\":\"x\",\"1\":\"y\"} num {} {} {} {}\nproto true __proto__,a undefined\nsetter false zz\norder 1,2,b,a 1\nthrew boom\nrest threw boom\ngetter {\"value\":42,\"writable\":true,\"enumerable\":true,\"configurable\":true}\noverride {\"a\":2,\"b\":3} {\"a\":9}\naccessor-target 2 {\"value\":2,\"writable\":true,\"enumerable\":true,\"configurable\":true}\nnonenum {}\nrest {\"b\":2} 1\narr rest {\"1\":6,\"2\":7}\nstr rest {\"0\":\"h\",\"1\":\"e\",\"2\":\"y\"}\nproxy {\"p\":1,\"q\":2}\nfn {\"k\":1}\nmap {} date {\"d\":1}\ntyped {\"0\":4,\"1\":5}\nbuiltin true max\nassign {\"0\":\"a\",\"1\":\"b\"} object {\"0\":\"x\",\"1\":\"y\"}\nassign setter true\nassign args {\"0\":1,\"1\":2}\nassign threw boom\nhot 1999000\n");
console.log('PASS object spread matches Node');
