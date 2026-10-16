const assert = require('node:assert');

const caught = fn => { try { fn(); } catch (e) { return e; } assert.fail('expected a throw'); };
const fields = e => [e.name, e.code, e.actual, e.expected, e.operator, e.generatedMessage];

assert.deepStrictEqual(fields(caught(() => assert.ok(0))), ['AssertionError', 'ERR_ASSERTION', 0, true, '==', true]);
assert.deepStrictEqual(fields(caught(() => assert.equal(1, 2))), ['AssertionError', 'ERR_ASSERTION', 1, 2, '==', true]);
assert.deepStrictEqual(fields(caught(() => assert.notEqual(1, 1))), ['AssertionError', 'ERR_ASSERTION', 1, 1, '!=', true]);
assert.deepStrictEqual(fields(caught(() => assert.strictEqual(1, 2, 'custom'))), ['AssertionError', 'ERR_ASSERTION', 1, 2, 'strictEqual', false]);
assert.deepStrictEqual(fields(caught(() => assert.notStrictEqual(1, 1))).slice(4), ['notStrictEqual', true]);
assert.strictEqual(caught(() => assert.deepEqual({ a: 1 }, { a: 2 })).operator, 'deepEqual');
assert.strictEqual(caught(() => assert.notDeepEqual(1, 1)).operator, 'notDeepEqual');
assert.strictEqual(caught(() => assert.deepStrictEqual([1], [2])).operator, 'deepStrictEqual');
assert.strictEqual(caught(() => assert.notDeepStrictEqual(1, 1)).operator, 'notDeepStrictEqual');
assert.strictEqual(caught(() => assert.match('abc', /x/)).operator, 'match');
assert.deepStrictEqual(fields(caught(() => assert.fail())), ['AssertionError', 'ERR_ASSERTION', undefined, undefined, 'fail', true]);

const own = new TypeError('own');
assert.strictEqual(caught(() => assert.fail(own)), own);
assert.strictEqual(caught(() => assert.ok(false, own)), own);

assert.strictEqual(caught(() => assert.throws(() => {})).message, 'Missing expected exception.');
assert.strictEqual(caught(() => assert.throws(() => {}, 'why')).message, 'Missing expected exception: why');
assert.strictEqual(caught(() => assert.throws(() => {}, TypeError)).message, 'Missing expected exception (TypeError).');
assert.strictEqual(caught(() => assert.throws(() => {}, TypeError, 'why')).message, 'Missing expected exception (TypeError): why');
assert.strictEqual(caught(() => assert.throws(() => {}, TypeError)).expected, TypeError);
assert.strictEqual(caught(() => assert.doesNotThrow(() => { throw new Error('zz'); }, 'why')).message, 'Got unwanted exception: why\nActual message: "zz"');

const err = caught(() => assert.ok(false, 'm'));
assert.strictEqual(String(err), 'AssertionError [ERR_ASSERTION]: m');
assert.strictEqual(err.stack.split('\n')[0], 'AssertionError [ERR_ASSERTION]: m');
assert.deepStrictEqual(Object.keys(err).filter(k => k !== 'diff'), ['generatedMessage', 'code', 'actual', 'expected', 'operator']);
assert.ok(err instanceof assert.AssertionError && err instanceof Error);

const coded = caught(() => Buffer.concat(1));
assert.strictEqual(String(coded).split(':')[0], 'TypeError [ERR_INVALID_ARG_TYPE]');
assert.strictEqual(coded.stack.split(':')[0], 'TypeError [ERR_INVALID_ARG_TYPE]');
const userCoded = Object.assign(new Error('u'), { code: 'ERR_USER' });
assert.strictEqual(String(userCoded), 'Error: u');
const fsErr = caught(() => require('fs').readFileSync('/definitely/missing/file'));
assert.ok(String(fsErr).startsWith('Error: ENOENT'), String(fsErr));

console.log('assert error fields OK');
