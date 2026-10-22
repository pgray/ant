const assert = require('node:assert');
const { spawnSync } = require('node:child_process');
const { mkdtempSync, writeFileSync, rmSync } = require('node:fs');
const { tmpdir } = require('node:os');
const path = require('node:path');

const dir = mkdtempSync(path.join(tmpdir(), 'ant-clipped-'));
const long = 'a'.repeat(400);

function render(source, color) {
  const file = path.join(dir, 'case.js');
  writeFileSync(file, source);
  const env = { ...process.env };
  delete env.FORCE_COLOR;
  delete env.NO_COLOR;
  if (color) env.FORCE_COLOR = '3';
  else env.NO_COLOR = '1';
  const child = spawnSync(process.execPath, [file], { encoding: 'utf8', env });
  assert.notStrictEqual(child.status, 0);
  return child.stderr.split('\n');
}

function contextLine(lines, n) {
  const strip = s => s.replace(/\x1b\[[0-9;]*m/g, '');
  const line = lines.find(l => strip(l).startsWith(`${n} | `));
  assert.ok(line, `context line ${n} missing`);
  return line;
}

const followers = 'const y = "b" + 1; // c\nundefinedFunction();\n';
const cases = [
  [`const x = "${long}";\n`, 'const x = "s";\n'],
  [`/* ${long} */ const z = 1;\n`, '/* s */ const z = 1;\n'],
  [`const t = \`${long}\`;\n`, 'const t = `s`;\n'],
  [`const r = /${long}/;\n`, 'const r = /s/;\n'],
];

try {
  for (const [clipped, reference] of cases) {
    const colored = render(clipped + followers, true);
    const expected = render(reference + followers, true);
    assert.strictEqual(contextLine(colored, 2), contextLine(expected, 2), clipped.slice(0, 12));
    assert.strictEqual(contextLine(colored, 3), contextLine(expected, 3), clipped.slice(0, 12));
    assert.ok(contextLine(colored, 1).replace(/\x1b\[[0-9;]*m/g, '').endsWith('...'));

    const plain = render(clipped + followers, false);
    assert.ok(!plain.join('\n').includes('\x1b['));
    const first = contextLine(plain, 1);
    assert.ok(first.endsWith('...'));
    assert.ok(clipped.startsWith(first.slice(4, -3)));
    assert.strictEqual(contextLine(plain, 2), '2 | const y = "b" + 1; // c');
    assert.strictEqual(contextLine(plain, 3), '3 | undefinedFunction();');
  }

  const unclipped = 'const a = "s";\nconst b = 2;\nundefinedFunction();\n';
  const plain = render(unclipped, false);
  assert.deepStrictEqual([1, 2, 3].map(n => contextLine(plain, n)), ['1 | const a = "s";', '2 | const b = 2;', '3 | undefinedFunction();']);
} finally {
  rmSync(dir, { recursive: true, force: true });
}

console.log('error context clipped line ok');
