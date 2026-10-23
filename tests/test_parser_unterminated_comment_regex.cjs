const assert = require('node:assert');
const { spawnSync } = require('node:child_process');
const vm = require('node:vm');

function rejects(source) {
  assert.throws(() => new vm.Script(source), SyntaxError, JSON.stringify(source));
}

function accepts(source) {
  assert.doesNotThrow(() => new vm.Script(source), JSON.stringify(source));
}

rejects('/*');
rejects('/* unterminated');
rejects('a /* x');
rejects('/* a */\n/* b');
rejects('const r = /ab');
rejects('x = /ab\nc/');
rejects('x = /ab\rc/');
rejects('x = /ab c/');
rejects('x = /ab\\\nc/');
rejects('x = /ab\\');
rejects('x = /[\n]/');
rejects('x = /=');

accepts('/**/');
accepts('/* ok */ 1');
accepts('x = 1 /* a\n b */ + 2');
accepts('x = /[/]/');
accepts('x = /a\\/b/g');
accepts('x = /=/');
accepts('x = a /= 2');
accepts('x = 4 / 2 / 1');

for (const ch of ['×', '→', '😀', '“']) {
  assert.throws(() => new vm.Script(`let ${ch} = {`), e => e instanceof SyntaxError && !e.message.includes('�'));
}

for (const source of ['console.log("ran"); /* unterminated', 'console.log("ran"); const r = /ab']) {
  const result = spawnSync(process.execPath, ['-e', source], { encoding: 'utf8' });
  assert.notStrictEqual(result.status, 0, source);
  assert.strictEqual(result.stdout, '', source);
  assert.match(result.stderr, /SyntaxError/, source);
}

for (const [source, locs] of [
  ['let 日本 = 1; null.x', ['1:18']],
  ['let s = "😀😀"; null.x', ['1:22']],
  ['const a = "é";\nfunction f() { const b = "😀"; throw new Error("x"); }\nf();', ['2:38', '3:1']],
  ['x = "é" + `😀`; [1].map(() => { "日"; null.y; })', ['1:43', '1:21']],
]) {
  try {
    vm.runInThisContext(source, { filename: 'cols.js' });
    assert.fail(source);
  } catch (e) {
    const found = [...String(e.stack).matchAll(/cols\.js:(\d+):(\d+)/g)].map(m => `${m[1]}:${m[2]}`);
    assert.deepStrictEqual(found, locs, source);
  }
}

if (typeof Ant !== 'undefined') {
  const located = spawnSync(process.execPath, ['--no-color', '-e', 'let ééé = 1 +* 2'], { encoding: 'utf8' });
  assert.match(located.stderr, /\[eval\]:1:14\n/, located.stderr);

  for (const [source, caret] of [
    ['let × = {', '    ^'],
    ['let ééé = 1 +* 2', '             ^'],
    ['x = "日本語', '    ^^^^^^^'],
  ]) {
    const result = spawnSync(process.execPath, ['--no-color', '-e', source], { encoding: 'utf8' });
    const lines = result.stderr.split('\n');
    const at = lines.findIndex(line => line.endsWith(source));
    assert.ok(at >= 0, result.stderr);
    assert.strictEqual(lines[at + 1].slice(lines[at].length - source.length), caret, result.stderr);
  }
}

console.log('parser unterminated comment/regex test passed');
