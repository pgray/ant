const assert = require('node:assert');
const util = require('node:util');
const { spawnSync } = require('node:child_process');
const fs = require('node:fs');
const os = require('node:os');
const path = require('node:path');

const lines = (s) => s.split('\n');

function makeTypeError() { return new TypeError('boom'); }
const te = makeTypeError();
assert.strictEqual(lines(te.stack)[0], 'TypeError: boom');
assert.match(lines(te.stack)[1], /^ {4}at makeTypeError \(.*test_error_stack_format\.cjs:\d+:\d+\)$/);
assert.doesNotMatch(te.stack, /\x1b/);

const desc = Object.getOwnPropertyDescriptor(te, 'stack');
assert.strictEqual(typeof desc.get, 'function');
assert.strictEqual(typeof desc.set, 'function');
assert.strictEqual(desc.enumerable, false);
assert.deepStrictEqual(Object.getOwnPropertyNames(te).sort(), ['message', 'stack']);
assert.strictEqual(Object.hasOwn(te, 'name'), false);
assert.deepStrictEqual(Object.keys(te), []);

class MyErr extends Error {}
const mine = new MyErr('x');
assert.strictEqual(mine.name, 'Error');
assert.strictEqual(Object.hasOwn(mine, 'name'), false);
assert.strictEqual(lines(mine.stack)[0], 'Error: x');
assert.strictEqual(lines(util.inspect(mine))[0], 'MyErr [Error]: x');

class MyError extends Error {}
assert.strictEqual(lines(util.inspect(new MyError('y')))[0], 'MyError: y');

class Named extends Error { constructor(m) { super(m); this.name = 'Named'; } }
const named = new Named('n');
assert.strictEqual(lines(named.stack)[0], 'Named: n');
assert.doesNotMatch(util.inspect(named), /name:/);

const src = fs.readFileSync(__filename, 'utf8').split('\n');
const pos = (marker, token) => {
  const i = src.findIndex((l) => l.includes(marker));
  return `${__filename}:${i + 1}:${src[i].indexOf(token) + 1}`;
};
const frame = (err, i = 1) => lines(err.stack)[i].trim();

function makeNamed() { const e = new Named('first-frame'); return e; }
assert.strictEqual(frame(makeNamed()), `at makeNamed (${pos("Named('first-frame')", 'new')})`);

const BoundNamed = Named.bind(null);
function makeBound() { const e = new BoundNamed('bound-frame'); return e; }
assert.strictEqual(frame(makeBound()), `at makeBound (${pos("BoundNamed('bound-frame')", 'new')})`);

class SubNamed extends Named {}
function makeSub() { const e = new SubNamed('sub-frame'); return e; }
assert.strictEqual(frame(makeSub()), `at makeSub (${pos("SubNamed('sub-frame')", 'new')})`);

class MyAgg extends AggregateError { constructor() { super([], 'agg-sub'); } }
class MySup extends SuppressedError { constructor() { super(1, 2, 'sup-sub'); } }
class DomSub extends DOMException { constructor() { super('dom-sub', 'AbortError'); } }
function makeAgg() { const e = new MyAgg(); return e; }
function makeSup() { const e = new MySup(); return e; }
function makeDom() { const e = new DomSub(); return e; }
assert.match(frame(makeAgg()), /^at makeAgg \(/);
assert.match(frame(makeSup()), /^at makeSup \(/);
assert.match(frame(makeDom()), /^at makeDom \(/);

assert.strictEqual(Reflect.construct(Error, ['detached'], function Detached() {}).stack, 'Error: detached');

function traceInner(fn) { const o = {}; Error.captureStackTrace(o, fn); return o.stack; }
function traceOuter(fn) { const s = traceInner(fn); return s; }
function traceCaller(fn) { const s = traceOuter(fn); return s; }
const traced = (fn) => ({ stack: traceCaller(fn) });
assert.match(frame(traced(traceOuter)), /^at traceCaller \(/);
assert.match(frame(traced(traceInner)), /^at traceOuter \(/);
assert.match(frame(traced(undefined)), /^at traceInner \(/);
assert.match(frame(traced(42)), /^at traceInner \(/);
assert.match(frame(traced(traceOuter.bind(null))), /^at traceInner \(/);
assert.match(frame(traced(Error.captureStackTrace)), /^at traceInner \(/);
assert.strictEqual(traceCaller(function notOnStack() {}), 'Error');
assert.strictEqual(traceCaller(Math.max), 'Error');
assert.strictEqual(Reflect.construct(TypeError, ['detached-native'], Map).stack, 'Error: detached-native');

class TypeSub extends TypeError {}
function makeTypeSub() { const e = new TypeSub('type-sub'); return e; }
assert.strictEqual(frame(makeTypeSub()), `at makeTypeSub (${pos("TypeSub('type-sub')", 'new')})`);

class NotCalled { constructor() {} }
let notCalled;
try { NotCalled(); } catch (e) { notCalled = e; }
assert.doesNotMatch(notCalled.stack, /at new NotCalled/);

queueMicrotask(function outermost() {
  const o = {};
  Error.captureStackTrace(o, outermost);
  assert.strictEqual(o.stack, 'Error');
});

class Built { constructor() { this.err = new Error('built'); } }
function FnBuilt() { this.err = new Error('fn-built'); }
class Holder extends Object { constructor() { super(); this.err = new Error('holder'); } }
assert.strictEqual(frame(new Built().err), `at new Built (${pos("Error('built')", 'new')})`);
assert.strictEqual(frame(new FnBuilt().err), `at new FnBuilt (${pos("Error('fn-built')", 'new')})`);
assert.strictEqual(frame(new Holder().err), `at new Holder (${pos("Error('holder')", 'new')})`);

class Throws { constructor(m) { throw new Error(m); } }
class ThrowsSub extends Throws {
  constructor() {
    super('via-super');
  }
}
let thrown;
try { new ThrowsSub(); } catch (e) { thrown = e; }
assert.strictEqual(frame(thrown), `at new Throws (${pos('throw new Error(m)', 'new')})`);
assert.strictEqual(frame(thrown, 2), `at new ThrowsSub (${pos("super('via-super')", 'super')})`);

function multiLineNew() {
  const e =
    new Error('multi-line');
  return e;
}
assert.strictEqual(frame(multiLineNew()), `at multiLineNew (${pos("Error('multi-line')", 'new')})`);

class SiteProbe { constructor() { const o = {}; Error.captureStackTrace(o); this.sites = o.stack; } }
const savedPrepare = Error.prepareStackTrace;
Error.prepareStackTrace = (_, cs) => cs;
const probeSites = new SiteProbe().sites;
const skippedSites = traceCaller(traceOuter);
Error.prepareStackTrace = savedPrepare;
assert.strictEqual(probeSites[0].isConstructor(), true);
assert.strictEqual(probeSites[1].isConstructor(), false);
assert.strictEqual(String(probeSites[0]), `new SiteProbe (${pos('this.sites = o.stack', 'captureStackTrace')})`);
assert.strictEqual(skippedSites[0].getFunctionName(), 'traceCaller');

const early = new Error('early');
const firstRead = early.stack;
early.message = 'changed';
assert.strictEqual(early.stack, firstRead);

const late = new Error('late');
late.message = 'changed';
assert.strictEqual(lines(late.stack)[0], 'Error: changed');

assert.strictEqual(lines(new Error().stack)[0], 'Error');
assert.strictEqual(lines(Object.create(new Error('base')).stack)[0], 'Error: base');

const assigned = new Error('a');
assigned.stack = 'custom';
assert.strictEqual(assigned.stack, 'custom');
assert.strictEqual(util.inspect(assigned), '[custom]');

for (const ctor of [AggregateError, SuppressedError]) {
  const err = ctor === AggregateError ? new AggregateError([], 'm') : new SuppressedError(1, 2, 'm');
  assert.strictEqual(Object.hasOwn(err, 'name'), false, ctor.name);
  assert.strictEqual(lines(err.stack)[0], `${ctor.name}: m`);
}

try { null.x; } catch (err) {
  assert.strictEqual(Object.hasOwn(err, 'name'), false);
  assert.match(lines(err.stack)[0], /^TypeError: /);
}

const target = {};
Error.captureStackTrace(target);
assert.strictEqual(lines(target.stack)[0], 'Error');
assert.deepStrictEqual(Object.keys(target), []);

const inspected = util.inspect(te);
assert.strictEqual(lines(inspected)[0], 'TypeError: boom');
assert.match(lines(inspected)[1], /^ {4}at makeTypeError /);

const withProps = new Error('outer', { cause: new Error('inner') });
withProps.code = 'E1';
const out = lines(util.inspect(withProps));
assert.match(out.find((l) => l.startsWith('    at')), / \{$/);
assert.ok(out.includes("  code: 'E1',"), out.join('\n'));
assert.ok(out.some((l) => l === '  [cause]: Error: inner'), out.join('\n'));
assert.strictEqual(out[out.length - 1], '}');

const nested = lines(util.inspect({ e: new RangeError('r') }));
assert.strictEqual(nested[1], '  e: RangeError: r');
assert.match(nested[2], /^ {6}at /);

const agg = util.inspect(new AggregateError([new Error('a')], 'agg'));
assert.match(agg, /\n {2}\[errors\]: \[\n {4}Error: a\n {8}at /);

function depthError(n) { const e = n ? depthError(n - 1) : new Error('d'); return e; }
const savedLimit = Error.stackTraceLimit;
assert.strictEqual(savedLimit, 10);
assert.strictEqual(lines(depthError(30).stack).length, 11);
Error.stackTraceLimit = 2;
assert.strictEqual(lines(depthError(30).stack).length, 3);
Error.stackTraceLimit = 1.9;
assert.strictEqual(lines(depthError(30).stack).length, 2);
Error.stackTraceLimit = 0;
assert.strictEqual(depthError(5).stack, 'Error: d');
Error.stackTraceLimit = 'x';
const unlimited = depthError(5);
assert.strictEqual(unlimited.stack, undefined);
assert.ok(Object.hasOwn(unlimited, 'stack'));
Error.stackTraceLimit = Infinity;
assert.ok(lines(depthError(40).stack).length > 41);
Error.stackTraceLimit = savedLimit;

const root = fs.mkdtempSync(path.join(os.tmpdir(), 'ant-error-stack-'));
try {
  const file = path.join(root, 'uncaught.cjs');
  fs.writeFileSync(file, "class E2 extends Error {}\nconst e = new E2('sub');\nconsole.log(JSON.stringify(e.stack.split('\\n')[0]));\nthrow e;\n");
  const result = spawnSync(process.execPath, [file], { encoding: 'utf8', env: { ...process.env, NO_COLOR: '1' } });
  const stderr = result.stderr.replace(/\x1b\[[0-9;]*m/g, '');
  assert.strictEqual(result.status, 1);
  assert.strictEqual(result.stdout.trim(), '"Error: sub"');
  assert.match(stderr, /uncaught\.cjs:2:\d+\n/);
  assert.match(stderr, /\n2 \| const e = new E2\('sub'\);\n/);
  assert.match(stderr, /\nE2 \[Error\]: sub\n {2}at /);

  const staleFile = path.join(root, 'stale.cjs');
  fs.writeFileSync(staleFile, [
    'function read(o) { try { return o.x; } catch { return 0; } }',
    'function warm() { for (let i = 0; i < 5000; i++) read({ x: i }); }',
    'warm();',
    'read(null);',
    'const g = { get v() { try { null.y; } catch {} return 1; } };',
    'g.v;',
    "throw new Error('later');",
    '',
  ].join('\n'));
  const stale = spawnSync(process.execPath, [staleFile], { encoding: 'utf8', env: { ...process.env, NO_COLOR: '1' } });
  const staleErr = stale.stderr.replace(/\x1b\[[0-9;]*m/g, '');
  assert.strictEqual(stale.status, 1);
  assert.match(staleErr, /^.*stale\.cjs:7:\d+\n/, staleErr);
  assert.doesNotMatch(staleErr, /stale\.cjs:[1-6]:/, staleErr);

  const handlerFile = path.join(root, 'handler.cjs');
  fs.writeFileSync(handlerFile, [
    "const e = new Error('real');",
    'let reads = 0;',
    "Object.defineProperty(e, 'name', { get() { if (reads++ === 0) throw new Error('name getter'); return 'Error'; } });",
    "process.on('uncaughtException', (err) => console.log(err === e, err.message));",
    'throw e;',
    '',
  ].join('\n'));
  const handler = spawnSync(process.execPath, [handlerFile], { encoding: 'utf8', env: { ...process.env, NO_COLOR: '1' } });
  assert.strictEqual(handler.stdout.trim(), 'true real', handler.stdout + handler.stderr);
} finally {
  fs.rmSync(root, { recursive: true, force: true });
}

console.log('error stack format ok');
