const assert = require('node:assert');
const { Readable, Writable } = require('node:stream');

const log = [];
let pending = 4;

function done() {
  if (--pending > 0) return;
  assert.deepStrictEqual(log, [
    'readable:construct',
    'writable:construct',
    'failing:construct',
    'readable:read',
    'writable:write:a',
    'writable:write:b',
    'writable:final',
    'failing:error:boom',
    'deferred:destroy:after-construct',
  ]);
  console.log('ok');
}

process.on('exit', () => assert.strictEqual(pending, 0, `streams still waiting: ${log}`));

const readable = new Readable({
  construct(callback) {
    log.push('readable:construct');
    setTimeout(callback, 10);
  },
  read() {
    log.push('readable:read');
    this.push('x');
    this.push(null);
  },
});
readable.on('data', () => {});
readable.on('end', done);

const writable = new Writable({
  construct(callback) {
    log.push('writable:construct');
    setTimeout(callback, 20);
  },
  write(chunk, encoding, callback) {
    log.push(`writable:write:${chunk}`);
    callback();
  },
  final(callback) {
    log.push('writable:final');
    callback();
  },
});
writable.write('a');
writable.end('b');
writable.on('finish', done);

class Failing extends Readable {
  _construct(callback) {
    log.push('failing:construct');
    setTimeout(() => callback(new Error('boom')), 30);
  }
  _read() {
    assert.fail('_read must not run when construct fails');
  }
}

const failing = new Failing();
failing.on('data', () => {});
failing.on('error', err => log.push(`failing:error:${err.message}`));
failing.on('close', done);

let constructed = false;
const deferred = new Readable({
  construct(callback) {
    setTimeout(() => {
      constructed = true;
      callback();
    }, 40);
  },
  read() {},
  destroy(err, callback) {
    log.push(`deferred:destroy:${constructed ? 'after-construct' : 'before-construct'}`);
    callback(err);
  },
});
deferred.destroy();
assert.strictEqual(deferred.destroyed, true);
deferred.on('close', done);
