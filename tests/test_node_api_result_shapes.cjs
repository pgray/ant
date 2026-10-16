const assert = require('node:assert');
const fs = require('node:fs');
const os = require('node:os');
const url = require('node:url');
const util = require('node:util');
const v8 = require('node:v8');

const plain = (label, value) => assert.strictEqual(Object.getPrototypeOf(value), Object.prototype, label);
const bare = (label, value) => assert.strictEqual(Object.getPrototypeOf(value), null, label);

plain('os.cpus()', os.cpus()[0]);
plain('os.cpus().times', os.cpus()[0].times);
plain('os.networkInterfaces()', os.networkInterfaces());
bare('os.userInfo()', os.userInfo());
plain('process.memoryUsage()', process.memoryUsage());
plain('process.cpuUsage()', process.cpuUsage());
plain('v8.getHeapStatistics()', v8.getHeapStatistics());
plain('v8.deserialize()', v8.deserialize(v8.serialize({ a: 1 })));
assert.ok(Object.prototype.isPrototypeOf(url.parse('http://a/b?c=1')), 'url.parse()');
bare('url.parse().query', url.parse('http://a/b?c=1', true).query);
plain('util.parseArgs()', util.parseArgs({ args: ['--x'], options: { x: { type: 'boolean' } } }));
bare('util.parseArgs().values', util.parseArgs({ args: ['--x'], options: { x: { type: 'boolean' } } }).values);
plain('Proxy.revocable()', Proxy.revocable({}, {}));
plain('TextEncoder.encodeInto()', new TextEncoder().encodeInto('a', new Uint8Array(4)));
plain('structuredClone()', structuredClone({ a: 1 }));
for (const C of [Intl.Collator, Intl.NumberFormat, Intl.DateTimeFormat, Intl.Segmenter]) {
  plain(`${C.name}.prototype`, C.prototype);
  plain(`${C.name} resolvedOptions()`, new C().resolvedOptions());
}

const cloned = structuredClone(new TypeError('x'));
assert.ok(cloned instanceof TypeError);
assert.strictEqual(cloned.message, 'x');
assert.deepStrictEqual(Object.keys(cloned), []);
assert.ok(structuredClone(new AggregateError([], 'a')) instanceof Error);

assert.strictEqual(JSON.stringify(Buffer.from('ab')), '{"type":"Buffer","data":[97,98]}');
assert.strictEqual(Buffer.from(JSON.parse(JSON.stringify(Buffer.from('hi')))).toString(), 'hi');
assert.deepStrictEqual([...Buffer.from([1.7, -1, 256, '3'])], [1, 255, 0, 3]);
assert.deepStrictEqual([...Buffer.from(new String('hi'))], [104, 105]);
assert.deepStrictEqual([...Buffer.from({ valueOf() { return 'ab'; } })], [97, 98]);
assert.deepStrictEqual([...Buffer.from({ [Symbol.toPrimitive]() { return 'ok'; } })], [111, 107]);
assert.deepStrictEqual([...Buffer.from({ length: 'x' })], []);
assert.throws(() => Buffer.from({}), {
  code: 'ERR_INVALID_ARG_TYPE',
  message: 'The first argument must be of type string or an instance of Buffer, ArrayBuffer, or Array or an Array-like Object. Received an instance of Object',
});

const usage = process.resourceUsage();
assert.deepStrictEqual(Object.keys(usage), [
  'userCPUTime', 'systemCPUTime', 'maxRSS', 'sharedMemorySize', 'unsharedDataSize', 'unsharedStackSize',
  'minorPageFault', 'majorPageFault', 'swappedOut', 'fsRead', 'fsWrite', 'ipcSent', 'ipcReceived',
  'signalsCount', 'voluntaryContextSwitches', 'involuntaryContextSwitches',
]);
assert.ok(usage.userCPUTime > 0 && usage.maxRSS > 0);

assert.strictEqual(JSON.parse(JSON.stringify(performance)).timeOrigin, performance.timeOrigin);

const stats = fs.statSync('.');
assert.ok(stats instanceof fs.Stats);
assert.strictEqual(stats.isDirectory(), true);
for (const name of ['isFile', 'isFIFO', 'isSocket', 'isBlockDevice', 'isCharacterDevice', 'isSymbolicLink'])
  assert.strictEqual(stats[name](), false, name);
if (process.platform !== 'win32') assert.strictEqual(fs.statSync('/dev/null').isCharacterDevice(), true);

const dirent = fs.readdirSync('.', { withFileTypes: true })[0];
assert.ok(dirent instanceof fs.Dirent);
assert.strictEqual(fs.Dirent.prototype.constructor, fs.Dirent);
assert.deepStrictEqual(Object.keys(fs.Dirent.prototype), []);
const made = new fs.Dirent('a', 1, '/p');
assert.deepStrictEqual([made.name, made.parentPath, made.isFile(), made.isDirectory()], ['a', '/p', true, false]);
assert.throws(() => fs.Dirent('a', 1), { name: 'TypeError' });

console.log('node api result shapes OK');
