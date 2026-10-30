import assert from 'node:assert/strict';
import childProcess from 'node:child_process';
import { familyNames, generateCases } from './probes.mjs';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import process from 'node:process';
import { minimizeArrays, parseArgs, runEngine, sameResult, sameVariantResult } from './runner.mjs';

assert.deepEqual(familyNames, [
  'property', 'regexp', 'promise', 'stream-shape', 'path',
  'json', 'number', 'string', 'structured-clone', 'textcodec', 'url', 'buffer-base64', 'jit'
]);

const first = generateCases({ families: familyNames, seed: 42, casesPerFamily: 2 });
const second = generateCases({ families: familyNames, seed: 42, casesPerFamily: 2 });
assert.deepEqual(
  first.map(testCase => testCase.build(testCase.params)),
  second.map(testCase => testCase.build(testCase.params)),
  'generation must be deterministic'
);
for (const testCase of first) assert.match(testCase.build(testCase.params), /console\.log\(JSON\.stringify\(__out\)\)/);
for (const testCase of first.filter(testCase => testCase.family === 'stream-shape')) {
  assert.equal(typeof testCase.params.scenario.target, 'string');
  assert.equal(typeof testCase.params.scenario.property, 'string');
  assert.deepEqual(testCase.shrinkKeys, [], 'stream cases must remain atomic');
}
for (const testCase of first.filter(testCase => testCase.family === 'path')) {
  assert.ok(testCase.params.scenarios.some(scenario => scenario.method === 'relative'));
  assert.ok(testCase.params.scenarios.some(scenario => scenario.method === 'resolve'));
  assert.deepEqual(testCase.shrinkKeys, ['scenarios']);
}
for (const testCase of first.filter(testCase => testCase.family === 'jit')) {
  assert.deepEqual(testCase.variants.map(variant => variant.name), ['default', 'jitless', 'opt:off', 'opt:force', 'opt:verify']);
  assert.match(testCase.build(testCase.params), /function kernel\(a, b, c\)/);
}
for (const testCase of first) {
  // generated probes must be valid scripts under the generating Node
  assert.doesNotThrow(() => new Function(testCase.build(testCase.params)), `${testCase.id} must parse`);
}
const pathCases = first.filter(testCase => testCase.family === 'path');
assert.notDeepEqual(
  pathCases[0].params.scenarios.slice(-400), pathCases[1].params.scenarios.slice(-400),
  'path probes must generate new input pairs, not just reorder fixed cases'
);

const parsed = parseArgs(['--family', 'regexp', '--cases', '3', '--seed', '9', '--timeout', '25', '--minimize', '--json']);
assert.deepEqual(parsed.families, ['regexp']);
assert.equal(parsed.casesPerFamily, 3);
assert.equal(parsed.seed, 9);
assert.equal(parsed.timeout, 25);
assert.equal(parsed.minimize, true);
assert.equal(parsed.json, true);
assert.throws(() => parseArgs(['--family', 'missing']), /unknown family/);
assert.throws(() => parseArgs(['--cases', '0']), /valid integer/);

const traced = { stderrFailures: 'codegen-failed' };
assert.equal(sameVariantResult({ stdout: 'x', stderr: '' }, { stdout: 'x', stderr: 'jit: opt compiled' }, traced), true);
assert.equal(sameVariantResult({ stdout: 'x', stderr: '' }, { stdout: 'x', stderr: 'codegen-failed func=f' }, traced), false);
assert.equal(sameVariantResult({ stdout: 'x', stderr: '' }, { stdout: 'y', stderr: '' }, traced), false);
assert.equal(sameResult({ stdout: 'x' }, { stdout: 'x' }), true);
assert.equal(sameResult({ stdout: 'x' }, { stdout: 'y' }), false);

const fakeCase = { shrinkKeys: ['items'] };
const minimized = minimizeArrays(fakeCase, { items: ['noise-a', 'keep', 'noise-b', 'noise-c'] }, candidate => candidate.items.includes('keep'));
assert.deepEqual(minimized, { items: ['keep'] });

const tempDir = fs.mkdtempSync(path.join(os.tmpdir(), 'ant-differential-test-'));
try {
  const probe = path.join(tempDir, 'probe.cjs');
  fs.writeFileSync(probe, "console.log('ok')\n");
  const envProbe = path.join(tempDir, 'env.cjs');
  fs.writeFileSync(envProbe, "console.log(process.env.PROBE_VARIANT, process.env.ANT_DEBUG ?? 'unset', process.execArgv.join(' '))\n");
  process.env.ANT_DEBUG = 'leaked';
  assert.equal(runEngine(process.execPath, envProbe, 1000).stdout, 'undefined unset \n', 'ANT_DEBUG must not leak into probes');
  assert.equal(
    runEngine(process.execPath, envProbe, 1000, { args: ['--no-warnings'], env: { PROBE_VARIANT: 'x', ANT_DEBUG: 'opt:off' } }).stdout,
    'x opt:off --no-warnings\n'
  );
  delete process.env.ANT_DEBUG;
  assert.deepEqual(runEngine(process.execPath, probe, 1000), {
    status: 0,
    signal: null,
    timedOut: false,
    stdout: 'ok\n',
    stderr: '',
    spawnError: null
  });
  const integration = childProcess.spawnSync(
    process.execPath,
    [
      'tests/differential/runner.mjs',
      '--node',
      process.execPath,
      '--ant',
      process.execPath,
      '--family',
      'property',
      '--cases',
      '1',
      '--seed',
      '1',
      '--json'
    ],
    { cwd: path.resolve(import.meta.dirname, '../..'), encoding: 'utf8' }
  );
  assert.equal(integration.status, 0, integration.stderr);
  assert.equal(JSON.parse(integration.stdout).match, true, 'the full runner should report equality when both engines are Node');
} finally {
  fs.rmSync(tempDir, { recursive: true, force: true });
}

console.log('differential runner tests passed');
