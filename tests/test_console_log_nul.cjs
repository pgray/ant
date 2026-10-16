const assert = require('node:assert');
const { spawnSync } = require('node:child_process');

const child = spawnSync(process.execPath, ['-e', 'console.log("x", "\\0"); console.log("y", "a\\0b"); console.log("a\\0b")']);
assert.strictEqual(child.status, 0, String(child.stderr));
assert.deepStrictEqual([...child.stdout], [...Buffer.from('x \0\ny a\0b\na\0b\n')]);

console.log('console.log NUL OK');
