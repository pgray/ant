function assert(condition, message) {
  if (!condition) throw new Error(message);
}

const bytes = new Uint8Array([72, 101, 108, 108, 111, 32, 87, 111, 114, 108, 100]);

assert(bytes.toHex() === "48656c6c6f20576f726c64", "Uint8Array.prototype.toHex");
assert(bytes.toBase64() === "SGVsbG8gV29ybGQ=", "Uint8Array.prototype.toBase64");

const fromHex = Uint8Array.fromHex("48656c6c6f20576f726c64");
assert(fromHex.length === bytes.length, "Uint8Array.fromHex length");
assert(bytes.every((value, index, array) => array === bytes && value === fromHex[index]), "Uint8Array.fromHex bytes");

const fromBase64 = Uint8Array.fromBase64("SGVsbG8gV29ybGQ=");
assert(fromBase64.length === bytes.length, "Uint8Array.fromBase64 length");
assert(bytes.every((value, index) => value === fromBase64[index]), "Uint8Array.fromBase64 bytes");

const hexTarget = new Uint8Array(16);
const hexResult = hexTarget.setFromHex("48656c6c6f20576f726c64");
assert(hexResult.read === 22, "Uint8Array.setFromHex read count");
assert(hexResult.written === 11, "Uint8Array.setFromHex written count");
assert(bytes.every((value, index) => value === hexTarget[index]), "Uint8Array.setFromHex bytes");

const base64Target = new Uint8Array(16);
const base64Result = base64Target.setFromBase64("SGVsbG8gV29ybGQ=");
assert(base64Result.read === 16, "Uint8Array.setFromBase64 read count");
assert(base64Result.written === 11, "Uint8Array.setFromBase64 written count");
assert(bytes.every((value, index) => value === base64Target[index]), "Uint8Array.setFromBase64 bytes");

const strict = require("node:assert");
const decode = (s, o) => Array.from(Uint8Array.fromBase64(s, o));
strict.deepStrictEqual(decode("YQ"), [97]);
strict.deepStrictEqual(decode(" YW Jj "), [97, 98, 99]);
strict.deepStrictEqual(decode("_-8", { alphabet: "base64url" }), [255, 239]);
strict.deepStrictEqual(decode("YWJj ZA", { lastChunkHandling: "stop-before-partial" }), [97, 98, 99]);
strict.deepStrictEqual(decode("YQ==", { lastChunkHandling: "strict" }), [97]);
strict.throws(() => decode("YR==", { lastChunkHandling: "strict" }), SyntaxError);
strict.throws(() => decode("YQ", { lastChunkHandling: "strict" }), SyntaxError);
strict.throws(() => decode("+/", { alphabet: "base64url" }), SyntaxError);
strict.throws(() => decode("Y"), SyntaxError);
strict.throws(() => decode("YQ", { alphabet: "nope" }), TypeError);
strict.throws(() => Uint8Array.fromBase64(123), TypeError);

const partialTarget = new Uint8Array(4);
strict.deepStrictEqual({ ...partialTarget.setFromBase64("YWJjZGVm") }, { read: 4, written: 3 });
strict.deepStrictEqual(Array.from(partialTarget), [97, 98, 99, 0]);

const encodeBytes = new Uint8Array([251, 255]);
strict.strictEqual(encodeBytes.toBase64(), "+/8=");
strict.strictEqual(encodeBytes.toBase64({ alphabet: "base64url" }), "-_8=");
strict.strictEqual(encodeBytes.toBase64({ alphabet: "base64url", omitPadding: true }), "-_8");
strict.throws(() => encodeBytes.toBase64({ alphabet: "x" }), TypeError);

console.log("Uint8Array base64/hex tests completed!");
