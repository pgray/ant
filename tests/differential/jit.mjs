// JIT tier differential: random arithmetic and control-flow kernels run hot
// enough to reach the template JIT and the optimizing tier, then with
// boundary and mistyped arguments that force bailouts. Node is the oracle,
// and every Ant variant (interpreter, template JIT only, forced and verified
// optimizing tier) must print what Node prints.

import { finish, int, literal, pick } from './common.mjs';

const WARM_CALLS = 3000;

// op-warn also traces every compile and bailout, so stderr is not compared;
// only the lines matching `stderrFailures` count as mismatches.
const stderrFailures = 'operand depth analysis failed|codegen-failed|verif(y|ier)[^\\n]*(fail|error)';
export const tierVariants = [
  { name: 'default', args: [], env: { ANT_DEBUG: 'dump/vm:op-warn' }, stderrFailures },
  { name: 'jitless', args: ['--jitless'], env: { ANT_DEBUG: 'dump/vm:op-warn' }, stderrFailures },
  { name: 'opt:off', args: [], env: { ANT_DEBUG: 'opt:off dump/vm:op-warn' }, stderrFailures },
  { name: 'opt:force', args: [], env: { ANT_DEBUG: 'opt:force,verify dump/vm:op-warn' }, stderrFailures },
  { name: 'opt:verify', args: [], env: { ANT_DEBUG: 'opt:verify dump/vm:op-warn' }, stderrFailures }
];

const intConstants = ['0', '1', '-1', '2', '3', '7', '31', '32', '255', '65535', '0x3fffffff', '0x7fffffff', '-0x80000000', '1e9'];
const doubleConstants = ['0.5', '-0', '1.5', '-2.25', '1e21', '1e-7', 'NaN', 'Infinity'];
const binaryOps = ['+', '-', '*', '/', '%', '|', '&', '^', '<<', '>>', '>>>'];
const compareOps = ['<', '<=', '>', '>=', '===', '!==', '==', '!='];
// only exactly specified Math functions: atan2, pow and the like may differ in the last bit
const calls = ['Math.imul', 'Math.max', 'Math.min'];
const unaryCalls = ['Math.floor', 'Math.ceil', 'Math.round', 'Math.trunc', 'Math.abs', 'Math.sign', 'Math.sqrt', 'Math.clz32', 'Math.fround'];

function expr(random, depth) {
  if (depth <= 0 || random() < 0.2) {
    const roll = random();
    if (roll < 0.55) return pick(random, ['a', 'b', 'c', 'r']);
    if (roll < 0.85) return pick(random, intConstants);
    return pick(random, doubleConstants);
  }
  const roll = random();
  const left = expr(random, depth - 1);
  if (roll < 0.45) return `(${left} ${pick(random, binaryOps)} ${expr(random, depth - 1)})`;
  if (roll < 0.55) return `(${left} ${pick(random, compareOps)} ${expr(random, depth - 1)} ? ${expr(random, depth - 1)} : ${left})`;
  if (roll < 0.65) return `${pick(random, calls)}(${left}, ${expr(random, depth - 1)})`;
  if (roll < 0.75) return `${pick(random, unaryCalls)}(${left})`;
  if (roll < 0.82) return `(${left} | 0)`;
  if (roll < 0.88) return `(${pick(random, ['-', '~', '+', '!'])}${left})`;
  if (roll < 0.94) return `(${left} ?? ${expr(random, depth - 1)})`;
  return `String(${left}).length`;
}

// Statement shapes that stress loops, element and property access, and closures.
const statementBuilders = [
  random => `for (let k = 0; k < (b & 7); k++) r = (r + k * ${expr(random, 1)}) | 0;`,
  random => `{ const arr = [a, b, c, ${expr(random, 1)}]; r = r + (arr[a & 7] ?? 1); }`,
  random => `{ const o = (a & 1) ? { x: a, y: b } : { y: b, x: a, z: c }; r = o.x - o.y + (o.z ?? ${expr(random, 1)}); }`,
  random => `if (r !== r || r === Infinity || r === -Infinity) r = ${expr(random, 1)};`,
  random => `{ const g = x => x * ${expr(random, 1)} + r; r = g(a); }`,
  random => `{ let s = 0; for (const v of [a, b]) s += v; r = s - r; }`,
  random => `switch (a & 3) { case 0: r += 1; break; case 1: r = ${expr(random, 1)}; break; default: r -= b; }`,
  random => `{ const t = typed[a & 7]; typed[b & 7] = r; r = t + ${expr(random, 1)}; }`,
  random => `try { if ((a & 15) === 3) throw r; } catch (e) { r = e + 1; }`,
  random => `r = (r > 1e6 || r < -1e6) ? r % 1000 : r;`
];

const warmArgs = [
  '[i % 100 - 50, (i * 7) % 33 + 1, i]',
  '[i, i & 15, (i * 13) % 7]',
  '[(i % 9) * 0.5, i % 4, -i]'
];

const boundaryArgs = [
  '0x7fffffff', '-0x80000000', '0x40000000', '-0', '0.5', 'NaN', 'Infinity', '-Infinity', '1e21', '2 ** 53',
  '"7"', '"x"', 'undefined', 'null', 'true', '[]', '{}', '10n'
];

function jitCase(random, index) {
  const returns = expr(random, 4);
  const statements = Array.from({ length: int(random, 0, 3) }, () => pick(random, statementBuilders)(random));
  const warm = pick(random, warmArgs);
  const finals = Array.from({ length: int(random, 8, 16) }, () =>
    [pick(random, boundaryArgs), pick(random, boundaryArgs.concat(['1', '3', '-5'])), String(int(random, -3, 3))]
  );
  return {
    family: 'jit',
    id: `jit-${index}`,
    shrinkKeys: ['statements', 'finals'],
    variants: tierVariants,
    params: { initial: expr(random, 2), returns, statements, warm, finals },
    build(params) {
      return finish(`const typed = new Int32Array(8);
function kernel(a, b, c) {
  let r = ${params.initial};
  ${params.statements.join('\n  ')}
  return ${params.returns};
}
function key(v) { return typeof v === 'number' && Object.is(v, -0) ? '-0' : typeof v + ':' + String(v); }
let hash = 0;
for (let i = 0; i < ${WARM_CALLS}; i++) {
  const [a, b, c] = ${params.warm};
  let v;
  try { v = kernel(a, b, c); } catch (e) { v = 'throws ' + e.name; }
  const s = key(v);
  for (let k = 0; k < s.length; k++) hash = (Math.imul(hash, 31) + s.charCodeAt(k)) | 0;
}
__record('warm', () => hash);
__record('typed', () => Array.from(typed));
${params.finals
  .map((args, i) => `__recordName(${literal(`call:${i}:${args.join(',')}`)}, () => kernel(${args.join(', ')}));`)
  .join('\n')}
__record('typed-after', () => Array.from(typed));`);
    }
  };
}

export const jitBuilders = { jit: jitCase };
