const assert = require('node:assert');
const fs = require('node:fs');
const os = require('node:os');
const path = require('node:path');

const tmp = fs.mkdtempSync(path.join(os.tmpdir(), 'ant-stream-open-'));
const file = path.join(tmp, 'data.txt');
fs.writeFileSync(file, 'hello');

const events = [];
let pending = 6;

process.on('exit', () => assert.strictEqual(pending, 0, `streams still waiting: ${events}`));

function done() {
  if (--pending > 0) return;
  fs.rmSync(tmp, { recursive: true, force: true });
  assert.deepStrictEqual(events.sort(), [
    'early:close:null',
    'early:open',
    'early:ready',
    'missing:error:ENOENT',
    'read:open:number',
    'read:ready',
    'subclass:sub',
    'write:open:number',
    'write:ready',
  ]);
  console.log('ok');
}

const rs = fs.createReadStream(file);
assert.strictEqual(rs.pending, true);
rs.on('open', fd => events.push(`read:open:${typeof fd}`));
rs.on('ready', () => {
  events.push('read:ready');
  assert.strictEqual(rs.pending, false);
  assert.strictEqual(typeof rs.fd, 'number');
  rs.destroy();
});
rs.on('close', done);

const ws = fs.createWriteStream(path.join(tmp, 'out.txt'));
ws.on('open', fd => events.push(`write:open:${typeof fd}`));
ws.on('ready', () => {
  events.push('write:ready');
  ws.end();
});
ws.on('close', done);

const missing = fs.createReadStream(path.join(tmp, 'nope.txt'));
missing.on('error', err => events.push(`missing:error:${err.code}`));
missing.on('close', done);

const fd = fs.openSync(file, 'r');
const withFd = fs.createReadStream(file, { fd });
withFd.on('open', () => assert.fail('open must not fire for a provided fd'));
withFd.on('ready', () => assert.fail('ready must not fire for a provided fd'));
let body = '';
withFd.setEncoding('utf8');
withFd.on('data', chunk => { body += chunk; });
withFd.on('close', () => {
  assert.strictEqual(body, 'hello');
  done();
});

const early = fs.createReadStream(file);
early.on('open', () => events.push('early:open'));
early.on('ready', () => events.push('early:ready'));
early.on('close', () => {
  events.push(`early:close:${early.fd}`);
  done();
});
early.destroy();

assert.strictEqual(typeof fs.ReadStream.prototype._read, 'function');
assert.strictEqual(Object.hasOwn(rs, '_read'), false);
assert.strictEqual(Object.hasOwn(ws, '_write'), false);

class Sub extends fs.ReadStream {
  _read() {
    this.push('sub');
    this.push(null);
  }
}
let subBody = '';
new Sub(file)
  .on('data', chunk => { subBody += chunk; })
  .on('end', () => events.push(`subclass:${subBody}`))
  .on('close', done);
