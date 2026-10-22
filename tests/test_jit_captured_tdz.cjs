const assert = require('node:assert');

const ITERATIONS = 5000;

for (const kind of ['let', 'const', 'class']) {
  for (const forwarded of [false, true]) {
    const decl = kind === 'class' ? 'class value {}' : `${kind} value = 1;`;
    const call = forwarded ? 'forward()' : 'read()';
    const src = `
      function read() { return value; }
      function forward() { return read(); }
      let count = 0;
      for (let i = 0; i < ${ITERATIONS}; i++) {
        try { ${call}; } catch (e) { if (e instanceof ReferenceError) count++; }
      }
      ${decl}
      let after;
      for (let i = 0; i < ${ITERATIONS}; i++) after = ${call};
      return [count, typeof after];
    `;
    const expected = [ITERATIONS, kind === 'class' ? 'function' : 'number'];
    assert.deepStrictEqual(Function(src)(), expected, `${kind} ${forwarded ? 'forwarded' : 'direct'}`);
  }
}

console.log('jit captured tdz ok');
