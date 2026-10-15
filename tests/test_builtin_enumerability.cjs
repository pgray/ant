// ECMAScript built-ins have no enumerable properties: constructors, prototypes,
// namespaces, iterator prototypes and RegExp/Intl instance internals stay out
// of for-in and Object.keys. Also covers class prototype/length descriptors,
// the Error constructor chain and proxy ownKeys. Output must match what Node prints.
const assert = require('node:assert');
const { spawnSync } = require('node:child_process');

const body = String.raw`
const names = ['Object','Function','Array','String','Number','Boolean','Symbol','BigInt','Math','JSON','Reflect','Atomics','Promise','Map','Set','WeakMap','WeakSet','WeakRef','FinalizationRegistry','Date','RegExp','Error','TypeError','AggregateError','ArrayBuffer','SharedArrayBuffer','DataView','Uint8Array','Float64Array','Proxy','Intl','Iterator'];
for (const n of names) { const v = globalThis[n]; const ks=[]; for (const k in v) ks.push(k); console.log(n, JSON.stringify(ks), v.prototype ? JSON.stringify(Object.keys(v.prototype)) : '-'); }
const insts = { u8: new Uint8Array(0), ab: new ArrayBuffer(0), dv: new DataView(new ArrayBuffer(1)), re: /x/g, date: new Date(0), map: new Map(), set: new Set(), wm: new WeakMap(), ws: new WeakSet(), prom: Promise.resolve(), it: [][Symbol.iterator](), mapit: new Map().keys(), setit: new Set().values(), strit: ''[Symbol.iterator](), gen: (function*(){})(), wr: new WeakRef({}), fr: new FinalizationRegistry(()=>{}), nf: new Intl.NumberFormat(), coll: new Intl.Collator(), dtf: new Intl.DateTimeFormat(), seg: new Intl.Segmenter() };
for (const [n, v] of Object.entries(insts)) { const ks=[]; for (const k in v) ks.push(k); console.log(n, JSON.stringify(ks), JSON.stringify(Object.keys(v))); }
console.log('re keys', JSON.stringify(Object.keys(/a/gi)), JSON.stringify({.../a/}), JSON.stringify(/a/), /a/g.global, /a/g.lastIndex);
const r = /a/g; r.lastIndex = 3; console.log('re lastIndex', r.lastIndex, JSON.stringify(Object.getOwnPropertyDescriptor(r, 'lastIndex')));
const d=(o,k)=>JSON.stringify(Object.getOwnPropertyDescriptor(o,k));
console.log('BYTES_PER_ELEMENT', d(Uint8Array, 'BYTES_PER_ELEMENT'), d(Uint8Array.prototype, 'BYTES_PER_ELEMENT'));
console.log('Number.MAX_SAFE_INTEGER', d(Number, 'MAX_SAFE_INTEGER'), 'Map.prototype', d(Map, 'prototype'), 'Map.name', d(Map, 'name'));
console.log('error ctors', Object.getPrototypeOf(TypeError) === Error, Object.getPrototypeOf(AggregateError) === Error, typeof TypeError.captureStackTrace, TypeError.stackTraceLimit);
class M { constructor(a,b){} }; class N extends M {}; class L {};
console.log('class', M.length, N.length, L.length, d(L,'length'), d(N,'length'), d(M,'prototype'), d(L,'prototype'), Object.keys(class K { static s = 1 }).join());
const sy=Symbol('s'); const tg={a:1,[sy]:2}; Object.defineProperty(tg,'h',{value:3});
const p=new Proxy(tg,{}); console.log('proxy', JSON.stringify(Object.getOwnPropertyNames(p)), Object.getOwnPropertySymbols(p).length, JSON.stringify(Object.keys(p)), JSON.stringify(Reflect.ownKeys(p).map(String)));
const q=new Proxy({}, { ownKeys(){ return ['x', sy]; }, getOwnPropertyDescriptor(){ return {value:1, enumerable:true, configurable:true}; } });
console.log('proxy trap', JSON.stringify(Object.getOwnPropertyNames(q)), Object.getOwnPropertySymbols(q).length);
`;

const env = { ...process.env, NO_COLOR: '1' };
delete env.FORCE_COLOR;
const child = spawnSync(process.execPath, ['-e', body], { encoding: 'utf8', timeout: 60000, env });
assert.strictEqual(child.status, 0, child.stderr);
assert.strictEqual(child.stdout, "Object [] []\nFunction [] []\nArray [] []\nString [] []\nNumber [] []\nBoolean [] []\nSymbol [] []\nBigInt [] []\nMath [] -\nJSON [] -\nReflect [] -\nAtomics [] -\nPromise [] []\nMap [] []\nSet [] []\nWeakMap [] []\nWeakSet [] []\nWeakRef [] []\nFinalizationRegistry [] []\nDate [] []\nRegExp [] []\nError [\"stackTraceLimit\"] []\nTypeError [\"stackTraceLimit\"] []\nAggregateError [\"stackTraceLimit\"] []\nArrayBuffer [] []\nSharedArrayBuffer [] []\nDataView [] []\nUint8Array [] []\nFloat64Array [] []\nProxy [] -\nIntl [] -\nIterator [] []\nu8 [] []\nab [] []\ndv [] []\nre [] []\ndate [] []\nmap [] []\nset [] []\nwm [] []\nws [] []\nprom [] []\nit [] []\nmapit [] []\nsetit [] []\nstrit [] []\ngen [] []\nwr [] []\nfr [] []\nnf [] []\ncoll [] []\ndtf [] []\nseg [] []\nre keys [] {} {} true 0\nre lastIndex 3 {\"value\":3,\"writable\":true,\"enumerable\":false,\"configurable\":false}\nBYTES_PER_ELEMENT {\"value\":1,\"writable\":false,\"enumerable\":false,\"configurable\":false} {\"value\":1,\"writable\":false,\"enumerable\":false,\"configurable\":false}\nNumber.MAX_SAFE_INTEGER {\"value\":9007199254740991,\"writable\":false,\"enumerable\":false,\"configurable\":false} Map.prototype {\"value\":{},\"writable\":false,\"enumerable\":false,\"configurable\":false} Map.name {\"value\":\"Map\",\"writable\":false,\"enumerable\":false,\"configurable\":true}\nerror ctors true true function 10\nclass 2 0 0 {\"value\":0,\"writable\":false,\"enumerable\":false,\"configurable\":true} {\"value\":0,\"writable\":false,\"enumerable\":false,\"configurable\":true} {\"value\":{},\"writable\":false,\"enumerable\":false,\"configurable\":false} {\"value\":{},\"writable\":false,\"enumerable\":false,\"configurable\":false} s\nproxy [\"a\",\"h\"] 1 [\"a\"] [\"a\",\"h\",\"Symbol(s)\"]\nproxy trap [\"x\"] 1\n");
console.log('PASS built-in enumerability matches Node');
