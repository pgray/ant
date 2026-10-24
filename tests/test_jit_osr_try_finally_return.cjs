const assert = require('node:assert');

const log = [];

function loopInTry(n) {
  try {
    for (let i = 0; ; i++) {
      if (i === n) return i;
    }
  } finally {
    log.push('finally');
  }
}

function caller(n) {
  const value = loopInTry(n);
  log.push('caller');
  return value + 1;
}

assert.strictEqual(caller(20000), 20001);
assert.deepStrictEqual(log, ['finally', 'caller']);

function throwInTry(n) {
  try {
    for (let i = 0; ; i++) {
      if (i === n) throw new Error('boom');
    }
  } catch (err) {
    log.push('catch');
    throw err;
  }
}

function throwCaller(n) {
  try {
    throwInTry(n);
  } catch (err) {
    return err.message;
  }
}

log.length = 0;
assert.strictEqual(throwCaller(20000), 'boom');
assert.deepStrictEqual(log, ['catch']);

function throwAfterTry(n) {
  try {
    for (let i = 0; ; i++) if (i === n) break;
  } catch (err) {
    log.push('stale catch');
  }
  throw new Error('outside');
}

function throwAfterTryCaller(n) {
  try {
    throwAfterTry(n);
  } catch (err) {
    return err.message;
  }
}

log.length = 0;
assert.strictEqual(throwAfterTryCaller(20000), 'outside');
assert.deepStrictEqual(log, []);

console.log('jit osr try/finally return: ok');
