import assert from 'node:assert/strict';
import vm from 'node:vm';
import { Isolate, Context, Script, Reference, Callback, ExternalCopy, copy } from 'ant:isolate';

const syncThrows = (fn, code) => {
  try {
    fn();
  } catch (e) {
    if (code) assert.equal(e.code, code);
    return e;
  }
  assert.fail('expected a throw');
};

assert.equal(typeof Isolate, 'function');
assert.throws(() => Isolate(), TypeError);
assert.throws(() => new Isolate({ thread: 'nope' }), TypeError);
assert.throws(() => new Isolate({ globals: 'nope' }), TypeError);
assert.equal(typeof Context, 'function');
assert.equal(typeof Script, 'function');
assert.equal(typeof Reference, 'function');

for (const thread of ['own', 'current']) {
  const iso = new Isolate({ memoryLimit: 64, thread });
  assert.equal(iso.isDisposed, false);

  const ctx = await iso.createContext();
  assert.ok(ctx instanceof Context);
  assert.equal(ctx.global.typeof, 'object');

  await ctx.global.set('input', copy({ a: 1, list: [1, 2] }));
  const logged = [];
  await ctx.global.set('log', new Callback((...a) => { logged.push(a); return a.length; }));

  const script = await iso.compileScript('log("hi", input.a); input.a + input.list.length', { filename: 'job.js' });
  assert.ok(script instanceof Script);
  assert.equal(await script.run(ctx), 3);
  assert.equal(script.runSync(ctx), 3);
  assert.deepEqual(logged, [['hi', 1], ['hi', 1]]);

  const other = iso.createContextSync();
  syncThrows(() => script.runSync(other));
  other.global.setSync('log', new Callback(() => 0));
  other.global.setSync('input', copy({ a: 10, list: [] }));
  assert.equal(script.runSync(other), 10);
  assert.equal(other.evalSync('typeof globalThis.log'), 'function');
  assert.equal(ctx.evalSync('typeof globalThis.nothing'), 'undefined');
  other.release();

  const fn = await ctx.evalReference('(x) => x * 2');
  assert.ok(fn instanceof Reference);
  assert.equal(fn.typeof, 'function');
  assert.equal(await fn.apply(undefined, [21]), 42);
  assert.equal(fn.applySync(undefined, [5]), 10);

  const add = ctx.evalReferenceSync('(a, b) => a + b');
  const callWith = ctx.evalReferenceSync('(f) => f(3, 4)');
  assert.equal(await callWith.apply(undefined, [add]), 7);

  assert.deepEqual(await ctx.eval('({ n: 1, s: "x", d: new Date(0).getTime(), a: [1, [2]] })'), { n: 1, s: 'x', d: 0, a: [1, [2]] });
  assert.deepEqual(ctx.evalSync('new Map([[1, 2]])'), new Map([[1, 2]]));

  await assert.rejects(ctx.eval('throw new TypeError("inside")'), e => e instanceof TypeError && e.message === 'inside');
  await assert.rejects(ctx.eval('throw 1'), e => e === 1);
  await assert.rejects(ctx.eval('(function () {})'));
  assert.ok(syncThrows(() => ctx.evalSync('null.x')) instanceof TypeError);

  await assert.rejects(ctx.eval('while (true) {}', { timeout: 50 }), { code: 'ERR_SCRIPT_EXECUTION_TIMEOUT' });
  syncThrows(() => ctx.evalSync('for (;;) {}', { timeout: 30 }), 'ERR_SCRIPT_EXECUTION_TIMEOUT');
  assert.equal(await ctx.eval('1 + 1'), 2);

  assert.equal(await ctx.eval('Promise.resolve(7)', { promise: true }), 7);
  await assert.rejects(ctx.eval('Promise.reject(new RangeError("later"))', { promise: true }), RangeError);

  const bareTypes = await ctx.eval('[typeof require, typeof process, typeof setTimeout, typeof console, typeof Array, typeof Intl, typeof WebAssembly]');
  assert.deepEqual(bareTypes, ['undefined', 'undefined', 'undefined', 'object', 'function', 'object', 'object']);
  await assert.rejects(ctx.eval('import("node:fs")', { promise: true }), { code: 'ERR_VM_DYNAMIC_IMPORT_CALLBACK_MISSING' });
  assert.equal(ctx.evalSync('typeof Function("return 1")'), 'function');

  assert.equal(await iso.mainContext.eval('typeof Array'), 'function');
  assert.equal(iso.mainContext.evalSync('typeof globalThis.input'), 'undefined');

  const stats = await iso.getHeapStatistics();
  assert.equal(typeof stats.used_heap_size, 'number');
  assert.ok(iso.getHeapStatisticsSync().total_heap_size > 0);
  assert.equal(typeof iso.cpuTime, 'bigint');
  assert.equal(typeof iso.wallTime, 'bigint');

  const ref = await ctx.global.get('input', { reference: true });
  assert.deepEqual(await ref.copy(), { a: 1, list: [1, 2] });
  assert.deepEqual(ref.copySync(), { a: 1, list: [1, 2] });
  ref.release();
  syncThrows(() => ref.copySync());

  const ext = new ExternalCopy({ deep: [1, 2, 3] });
  const into = ext.copyInto();
  ext.release();
  assert.throws(() => ext.copy(), TypeError);
  assert.throws(() => ctx.global.set('gone', ext), TypeError);
  await ctx.global.set('ext', into);
  assert.equal(ctx.evalSync('ext.deep.length'), 3);
  assert.deepEqual(into.copy(), { deep: [1, 2, 3] });

  const scratch = iso.createContextSync();
  scratch.evalSync('globalThis.tag = "scratch"');
  const once = iso.compileScriptSync('tag');
  assert.equal(once.runSync(scratch), 'scratch');
  once.release();
  assert.throws(() => once.runSync(scratch), /released/);
  scratch.release();
  assert.throws(() => scratch.evalSync('1'), /released/);
  assert.equal(ctx.evalSync('1 + 2'), 3);

  const kept = [];
  for (let i = 0; i < 6; i++) {
    const c = iso.createContextSync();
    c.evalSync(`globalThis.tag = ${i}`);
    c.global.release();
    kept.push(c);
  }
  for (let r = 0; r < 4; r++) iso.mainContext.evalSync('(() => { const a = []; for (let i = 0; i < 2e5; i++) a.push({ i }); return a.length })()');
  assert.deepEqual(kept.map(c => c.evalSync('tag')), [0, 1, 2, 3, 4, 5]);

  const sab = new SharedArrayBuffer(4);
  await ctx.global.set('sab', sab);
  await ctx.eval('new Int32Array(sab)[0] = 42');
  assert.equal(new Int32Array(sab)[0], 42);

  await ctx.global.set('boom', new Callback(() => { throw new RangeError('host boom'); }));
  assert.equal(ctx.evalSync('try { boom() } catch (e) { e.constructor.name + ":" + e.message }'), 'RangeError:host boom');

  assert.equal(ctx.evalSync('(function f(n) { return n ? f(n - 1) + 1 : 0 })(2000)'), 2000);
  assert.ok(syncThrows(() => ctx.evalSync('(function f() { return 1 + f() })()')) instanceof RangeError);
  assert.equal(ctx.evalSync('1 + 1'), 2);

  iso.dispose();
  assert.equal(iso.isDisposed, true);
  await assert.rejects(ctx.eval('1'), { code: 'ERR_ISOLATE_DISPOSED' });
  syncThrows(() => ctx.evalSync('1'), 'ERR_ISOLATE_DISPOSED');
  iso.dispose();

  const small = new Isolate({ thread, memoryLimit: 16 });
  const sctx = small.createContextSync();
  await assert.rejects(sctx.eval('const keep = []; for (;;) keep.push({ n: keep.length })'), { code: 'ERR_ISOLATE_OUT_OF_MEMORY' });
  assert.equal(small.isDisposed, true);
  await assert.rejects(sctx.eval('1 + 1'), { code: 'ERR_ISOLATE_DISPOSED' });

  const arrays = new Isolate({ thread, memoryLimit: 16 });
  syncThrows(() => arrays.createContextSync().evalSync('let held = []; for (;;) held.push([held.length, 1])'), 'ERR_ISOLATE_OUT_OF_MEMORY');

  for (let i = 0; i < 20; i++) {
    const churn = new Isolate({ thread, memoryLimit: 16 });
    assert.equal(churn.createContextSync().evalSync(`${i} * 2`), i * 2);
    churn.dispose();
  }

  const antIso = new Isolate({ thread, globals: 'ant' });
  assert.deepEqual(await antIso.mainContext.eval('[typeof require, typeof process, typeof setTimeout]'), ['undefined', 'object', 'function']);
  assert.equal(await antIso.mainContext.eval('new Promise(r => setTimeout(() => r("timer"), 10))', { promise: true }), 'timer');
  antIso.dispose();
}

{
  const iso = new Isolate();
  const ctx = await iso.createContext();
  await ctx.global.set('slow', new Callback(async x => { await new Promise(r => setTimeout(r, 10)); return x + 1; }));
  assert.equal(await ctx.eval('slow(1)'), 2);

  const pending = ctx.eval('while (true) {}');
  setTimeout(() => iso.dispose(), 30);
  await assert.rejects(pending, { code: 'ERR_ISOLATE_DISPOSED' });
}

{
  const iso = new Isolate();
  const ctx = iso.createContextSync();
  ctx.global.setSync('killme', new Callback(() => { iso.dispose(); return 1; }));
  await assert.rejects(ctx.eval('try { killme() } catch {} for (;;) {}'), { code: 'ERR_ISOLATE_DISPOSED' });
}

{
  const iso = new Isolate();
  const ctx = iso.createContextSync();
  let settle;
  ctx.global.setSync('later', new Callback(() => new Promise(r => { settle = r; })));
  const pending = ctx.eval('later()');
  while (!settle) await new Promise(r => setTimeout(r, 1));
  iso.dispose();
  settle(5);
  await assert.rejects(pending, { code: 'ERR_ISOLATE_DISPOSED' });
  await new Promise(r => setTimeout(r, 5));
}

{
  const outer = new Isolate({ globals: 'ant' });
  const nested = await outer.mainContext.eval(`
    import('ant:isolate').then(async ({ Isolate }) => {
      const inner = new Isolate();
      const v = (await inner.createContext()).evalSync('6 * 7');
      inner.dispose();
      return v;
    })`, { promise: true });
  assert.equal(nested, 42);
  outer.dispose();
}

{
  const iso = new Isolate({ thread: 'current' });
  const ctx = iso.createContextSync();
  await ctx.global.set('slow', new Callback(async x => x + 1));
  assert.match(ctx.evalSync('try { slow(1) } catch (e) { e.message }'), /promise/i);

  const double = ctx.evalReferenceSync('(x) => x * 2');
  ctx.global.setSync('reenter', new Callback(x => double.applySync(undefined, [x]) + 1));
  assert.equal(ctx.evalSync('reenter(20)'), 41);

  ctx.global.setSync('churn', new Callback(() => {
    const a = [];
    for (let i = 0; i < 2e5; i++) a.push({ i });
    globalThis.gc?.();
    return a.length;
  }));
  assert.equal(ctx.evalSync('let k = []; for (let i = 0; i < 5e4; i++) k.push({ i }); churn() + k.length'), 250000);

  const fromVm = vm.runInNewContext('(c) => c.evalSync("[1, 2, 3].map(x => x * 2)")')(ctx);
  assert.deepEqual([...fromVm], [2, 4, 6]);

  ctx.global.setSync('killme', new Callback(() => { iso.dispose(); return 1; }));
  syncThrows(() => ctx.evalSync('try { killme() } catch {} 2'), 'ERR_ISOLATE_DISPOSED');
  assert.equal(iso.isDisposed, true);
}

{
  const iso = new Isolate();
  const ctx = await iso.createContext();
  for (let i = 0; i < 500; i++) await ctx.global.get('Array', { reference: true });
  globalThis.gc?.();
  assert.equal(await ctx.eval('[].length'), 0);
  iso.dispose();
}

const live = new Isolate({ thread: 'current', globals: 'ant' });
live.mainContext.evalSync('setTimeout(() => {}, 20)');
const leak = new Isolate();
await leak.createContext();

console.log('ok');
