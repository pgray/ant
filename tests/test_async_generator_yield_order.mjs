const log = [];
async function* g() { const r = yield 1; log.push('got ' + r); yield 'two'; yield { obj: 3 }; yield Promise.resolve(4); return 5; }
const it = g();
it.next().then(v => log.push('A ' + JSON.stringify(v)));
Promise.resolve().then(() => log.push('B'));
const p2 = it.next('x');
Promise.resolve().then(() => log.push('C'));
await p2.then(v => log.push('D ' + JSON.stringify(v)));
log.push('E ' + JSON.stringify(await it.next()));
log.push('F ' + JSON.stringify(await it.next()));
log.push('G ' + JSON.stringify(await it.next()));
log.push('H ' + JSON.stringify(await it.next()));
async function* h() { try { yield 1; yield 2; } finally { log.push('cleanup'); } }
const ih = h(); await ih.next();
log.push('R ' + JSON.stringify(await ih.return(9)));
async function* k() { try { yield 1; } catch (e) { log.push('caught ' + e); yield 'after'; } }
const ik = k(); await ik.next();
log.push('T ' + JSON.stringify(await ik.throw('boom')));
const all = []; for await (const v of (async function* () { for (let i = 0; i < 5; i++) yield i; })()) all.push(v);
log.push('loop ' + all.join(','));
const expected = ["got x", "B", "C", "A {\"value\":1,\"done\":false}", "D {\"value\":\"two\",\"done\":false}", "E {\"value\":{\"obj\":3},\"done\":false}", "F {\"value\":4,\"done\":false}", "G {\"value\":5,\"done\":true}", "H {\"done\":true}", "cleanup", "R {\"value\":9,\"done\":true}", "caught boom", "T {\"value\":\"after\",\"done\":false}", "loop 0,1,2,3,4"];
if (JSON.stringify(log) !== JSON.stringify(expected)) throw new Error('order mismatch:\n' + log.join('\n'));
console.log('async generator yield order OK');
