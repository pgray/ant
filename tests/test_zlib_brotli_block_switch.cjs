const assert = require('node:assert');
const zlib = require('node:zlib');

const invalidBlockSwitch = Buffer.from('200010414141fcffff07002228f00b60f7f41f000000', 'hex');
const expectedError = {
  code: 'ERR__ERROR_FORMAT_BLOCK_SWITCH',
  errno: zlib.constants.BROTLI_DECODER_ERROR_FORMAT_BLOCK_SWITCH,
};

assert.strictEqual(zlib.constants.BROTLI_DECODER_ERROR_FORMAT_BLOCK_SWITCH, -17);
assert.strictEqual(zlib.constants.BROTLI_DECODER_NO_ERROR, 0);
assert.strictEqual(zlib.constants.BROTLI_DECODER_ERROR_UNREACHABLE, -31);
assert.throws(() => zlib.brotliDecompressSync(invalidBlockSwitch), expectedError);

const payload = Buffer.from('brotli round trip '.repeat(4096));
assert.deepStrictEqual(zlib.brotliDecompressSync(zlib.brotliCompressSync(payload)), payload);

let errored = false;
let ended = false;
const brotli = zlib.createBrotliDecompress();
brotli.resume();
brotli.on('error', error => {
  assert.strictEqual(error.code, expectedError.code);
  assert.strictEqual(error.errno, expectedError.errno);
  errored = true;
});
brotli.on('end', () => {
  ended = true;
});
for (let i = 0; i < invalidBlockSwitch.length; i++) brotli.write(invalidBlockSwitch.subarray(i, i + 1));
brotli.end();

process.on('exit', () => {
  assert.strictEqual(errored, true);
  assert.strictEqual(ended, false);
  console.log('ok');
});
