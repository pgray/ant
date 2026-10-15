// Object.defineProperties/Object.create read only enumerable own keys
// (symbols included), validate every descriptor before defining any, and
// report bad descriptors with V8's messages. Output must match what Node prints.
const assert = require('node:assert');
const { spawnSync } = require('node:child_process');

const body = String.raw`
const s=v=>JSON.stringify(v);
const t=(n,f)=>{ try { console.log(n, s(f())); } catch (e) { console.log(n, 'throws', e.name, e.message); } };
const p={}; Object.defineProperty(p,'x',{value:5}); p.y={value:1,enumerable:true};
t('nonenum', ()=>Object.getOwnPropertyNames(Object.defineProperties({}, p)));
const sym=Symbol('s'); t('symbol', ()=>Object.getOwnPropertySymbols(Object.defineProperties({}, {[sym]:{value:3, enumerable:true}})).length);
const arr=[{value:1,enumerable:true}]; arr.foo={value:2,enumerable:true}; t('array', ()=>Object.keys(Object.defineProperties({}, arr)));
function A(){ return arguments; } t('args', ()=>Object.keys(Object.defineProperties({}, A({value:1,enumerable:true}))));
t('atomic', ()=>{ const o={}; try { Object.defineProperties(o, {a:{value:1}, b:5}); } catch(e) { return [e.name, Object.getOwnPropertyNames(o)]; } });
t('null', ()=>Object.defineProperties({}, null)); t('undef', ()=>Object.defineProperties({}, undefined));
t('string', ()=>Object.defineProperties({}, 'ab')); t('emptystr', ()=>Object.keys(Object.defineProperties({}, '')));
t('number', ()=>Object.keys(Object.defineProperties({}, 5)));
t('proxy', ()=>Object.keys(Object.defineProperties({}, new Proxy({q:{value:1,enumerable:true}},{}))));
t('getter desc', ()=>{ const o=Object.defineProperties({}, {g:{get(){return 7}, enumerable:true}}); return o.g; });
t('accessor src', ()=>{ let n=0; const src={get k(){ n++; return {value:n, enumerable:true}; }}; const o=Object.defineProperties({}, src); return [o.k, n]; });
t('create', ()=>Object.keys(Object.create(null, p)));
t('target prim', ()=>Object.defineProperties(1, {}));
t('order', ()=>Object.keys(Object.defineProperties({}, {b:{value:1,enumerable:true}, 1:{value:2,enumerable:true}, a:{value:3,enumerable:true}})));
t('bad getter', ()=>Object.defineProperties({}, {g:{get: 5}}));
for (const v of [5,'ab',null,true,undefined,Symbol('q'),[1],{}, new Map()]) t('desc', ()=>Object.defineProperty({}, 'a', v));
for (const v of [5,'x',{},null,[1]]) t('get', ()=>Object.defineProperty({}, 'g', {get: v}));
t('set', ()=>Object.defineProperty({}, 'g', {set: 5}));
t('fn desc', ()=>Object.getOwnPropertyDescriptor(Object.defineProperty({}, 'a', Object.assign(function(){}, {value: 3, enumerable: true})), 'a'));
t('arr desc', ()=>Object.getOwnPropertyDescriptor(Object.defineProperty({}, 'a', Object.assign([], {value: 4})), 'a'));
t('proxy desc', ()=>Object.defineProperty({}, 'a', new Proxy({value: 9, writable: true}, {})).a);
t('both', ()=>Object.defineProperty({}, 'a', {value: 1, get(){}}));
`;

const env = { ...process.env, NO_COLOR: '1' };
delete env.FORCE_COLOR;
const child = spawnSync(process.execPath, ['-e', body], { encoding: 'utf8', timeout: 60000, env });
assert.strictEqual(child.status, 0, child.stderr);
assert.strictEqual(child.stdout, "nonenum [\"y\"]\nsymbol 1\narray [\"0\",\"foo\"]\nargs [\"0\"]\natomic [\"TypeError\",[]]\nnull throws TypeError Cannot convert undefined or null to object\nundef throws TypeError Cannot convert undefined or null to object\nstring throws TypeError Property description must be an object: a\nemptystr []\nnumber []\nproxy [\"q\"]\ngetter desc 7\naccessor src [1,1]\ncreate [\"y\"]\ntarget prim throws TypeError Object.defineProperties called on non-object\norder [\"1\",\"b\",\"a\"]\nbad getter throws TypeError Getter must be a function: 5\ndesc throws TypeError Property description must be an object: 5\ndesc throws TypeError Property description must be an object: ab\ndesc throws TypeError Property description must be an object: null\ndesc throws TypeError Property description must be an object: true\ndesc throws TypeError Property description must be an object: undefined\ndesc throws TypeError Property description must be an object: Symbol(q)\ndesc {}\ndesc {}\ndesc {}\nget throws TypeError Getter must be a function: 5\nget throws TypeError Getter must be a function: x\nget throws TypeError Getter must be a function: #<Object>\nget throws TypeError Getter must be a function: null\nget throws TypeError Getter must be a function: [object Array]\nset throws TypeError Setter must be a function: 5\nfn desc {\"value\":3,\"writable\":false,\"enumerable\":true,\"configurable\":false}\narr desc {\"value\":4,\"writable\":false,\"enumerable\":false,\"configurable\":false}\nproxy desc 9\nboth throws TypeError Invalid property descriptor. Cannot both specify accessors and a value or writable attribute, #<Object>\n");
console.log('PASS defineProperties matches Node');
