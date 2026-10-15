// node:assert failures throw AssertionError (code ERR_ASSERTION,
// generatedMessage) instead of TypeError, and arguments objects are never
// deep-equal to arrays. Output must match what Node prints.
const assert = require('node:assert');
const { spawnSync } = require('node:child_process');

const body = String.raw`
const a=require('assert');
const show=f=>{ try { f(); console.log('ok'); } catch (e) { console.log(e.name, e.code, e instanceof a.AssertionError, e instanceof Error, e.generatedMessage); } };
const showMsg=f=>{ try { f(); console.log('ok'); } catch (e) { console.log(e.name, e.code, e instanceof a.AssertionError, e.generatedMessage, JSON.stringify(e.message.split('\n')[0])); } };
showMsg(()=>a.ifError(new Error('boom'))); showMsg(()=>a.ifError('str')); show(()=>a.ifError(null)); show(()=>a.ifError(undefined));
showMsg(()=>a.doesNotThrow(()=>{throw new Error('zz')})); showMsg(()=>a.fail()); showMsg(()=>a.fail('x'));
show(()=>a.ok(0)); showMsg(()=>a(0,'m')); showMsg(()=>a.throws(()=>{})); showMsg(()=>a.strictEqual(1,2,'custom'));
show(()=>a.strictEqual(1,2)); show(()=>a.equal(1,2)); show(()=>a.deepEqual({a:1},{a:2}));
function A(){return arguments}
show(()=>a.deepStrictEqual(A(1,2), [1,2])); show(()=>a.deepEqual(A(1,2), [1,2])); show(()=>a.deepStrictEqual(A(1,2), A(1,2)));
show(()=>a.deepStrictEqual([1,2], [1,2])); show(()=>a.notDeepStrictEqual(A(1), [1]));
const e = new a.AssertionError({message: 'm', actual: 1, expected: 2, operator: '==='});
console.log(e.name, e instanceof Error, e.message, e.actual, e.expected, e.operator);
`;

const env = { ...process.env, NO_COLOR: '1' };
delete env.FORCE_COLOR;
const child = spawnSync(process.execPath, ['-e', body], { encoding: 'utf8', timeout: 60000, env });
assert.strictEqual(child.status, 0, child.stderr);
assert.strictEqual(child.stdout, "AssertionError ERR_ASSERTION true false \"ifError got unwanted exception: boom\"\nAssertionError ERR_ASSERTION true false \"ifError got unwanted exception: 'str'\"\nok\nok\nAssertionError ERR_ASSERTION true false \"Got unwanted exception.\"\nAssertionError ERR_ASSERTION true true \"Failed\"\nAssertionError ERR_ASSERTION true false \"x\"\nAssertionError ERR_ASSERTION true true true\nAssertionError ERR_ASSERTION true false \"m\"\nAssertionError ERR_ASSERTION true false \"Missing expected exception.\"\nAssertionError ERR_ASSERTION true false \"custom\"\nAssertionError ERR_ASSERTION true true true\nAssertionError ERR_ASSERTION true true true\nAssertionError ERR_ASSERTION true true true\nAssertionError ERR_ASSERTION true true true\nAssertionError ERR_ASSERTION true true true\nok\nok\nok\nAssertionError true m 1 2 ===\n");
console.log('PASS assert errors match Node');
