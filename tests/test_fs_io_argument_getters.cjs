const assert = require('node:assert');
const fs = require('node:fs');
const os = require('node:os');
const path = require('node:path');

const dir = fs.mkdtempSync(path.join(os.tmpdir(), 'ant-fs-io-'));
const file = name => path.join(dir, name);

function shifting(first, later) {
  const list = [null];
  let reads = 0;
  Object.defineProperty(list, 0, { get: () => (reads++ === 0 ? first : later) });
  return list;
}

function testReadSyncBounds() {
  const fd = fs.openSync(__filename, 'r');
  const buf = Buffer.alloc(16);
  try {
    for (const [offset, length] of [[0, -1], [8, -8], [4, 0xffffffff], [17, 1], [-1, 1]]) {
      assert.throws(() => fs.readSync(fd, buf, offset, length, 0), RangeError, `${offset}/${length}`);
      assert.throws(() => fs.readSync(fd, buf, { offset, length, position: 0 }), RangeError, `{${offset}/${length}}`);
    }
    for (const read of [() => fs.readSync(fd, buf, 1, 2 ** 64, 0), () => fs.readSync(fd, buf, { offset: 1, length: 2 ** 64 })]) {
      try {
        assert.strictEqual(read(), 0);
      } catch (err) {
        assert.ok(err instanceof RangeError, err);
      }
    }
    assert.strictEqual(fs.readSync(fd, buf, 4, 12, 0), 12);
    assert.strictEqual(buf.subarray(4).toString(), fs.readFileSync(__filename, 'utf8').slice(0, 12));
  } finally {
    fs.closeSync(fd);
  }
}

function testReadSyncOptionGetterDetach() {
  const fd = fs.openSync(__filename, 'r');
  const target = new Uint8Array(new ArrayBuffer(4096));
  const options = {
    get offset() {
      if (!target.buffer.detached) target.buffer.transfer();
      return 0;
    },
    length: 4096,
    position: 0,
  };
  try {
    assert.throws(() => fs.readSync(fd, target, options));
    assert.strictEqual(target.byteLength, 0);
  } finally {
    fs.closeSync(fd);
  }
}

function testWriteSyncOptionGetterDetach() {
  const fd = fs.openSync(file('write-sync'), 'w');
  const source = new Uint8Array(new ArrayBuffer(4096)).fill(0x41);
  const options = {
    get offset() {
      if (!source.buffer.detached) source.buffer.transfer();
      return 0;
    },
    length: 4096,
  };
  try {
    assert.throws(() => fs.writeSync(fd, source, options));
  } finally {
    fs.closeSync(fd);
  }
  assert.strictEqual(fs.statSync(file('write-sync')).size, 0);
}

function testWritevSyncGetterDetach() {
  const fd = fs.openSync(file('writev-sync'), 'w');
  const victim = new Uint8Array(new ArrayBuffer(1 << 16)).fill(0x41);
  const list = [victim, null];
  Object.defineProperty(list, 1, {
    get() {
      if (!victim.buffer.detached) victim.buffer.transfer();
      return Buffer.from('z');
    },
  });
  let written;
  try {
    written = fs.writevSync(fd, list);
  } finally {
    fs.closeSync(fd);
  }
  assert.strictEqual(written, 1);
  assert.strictEqual(fs.readFileSync(file('writev-sync'), 'utf8'), 'z');
}

function testWritevSyncShiftingEntry() {
  const fd = fs.openSync(file('writev-sync-shift'), 'w');
  let written;
  try {
    written = fs.writevSync(fd, shifting(Buffer.from('a'), Buffer.alloc(1 << 20, 0x42)));
  } finally {
    fs.closeSync(fd);
  }
  assert.strictEqual(fs.statSync(file('writev-sync-shift')).size, written);
}

function testWritevShiftingEntry() {
  const fd = fs.openSync(file('writev-shift'), 'w');
  return new Promise((resolve, reject) => {
    fs.writev(fd, shifting(Buffer.from('a'), Buffer.alloc(1 << 20, 0x42)), (err, written) => {
      fs.closeSync(fd);
      if (err) return reject(err);
      try {
        assert.strictEqual(fs.statSync(file('writev-shift')).size, written);
        resolve();
      } catch (e) {
        reject(e);
      }
    });
  });
}

async function main() {
  try {
    testReadSyncBounds();
    testReadSyncOptionGetterDetach();
    testWriteSyncOptionGetterDetach();
    testWritevSyncGetterDetach();
    testWritevSyncShiftingEntry();
    await testWritevShiftingEntry();
  } finally {
    fs.rmSync(dir, { recursive: true, force: true });
  }
  console.log('fs io argument getter tests passed');
}

main().catch(err => {
  console.error(err);
  process.exit(1);
});
