const assert = require('node:assert/strict');
const { Worker } = require('node:worker_threads');

const reader = `
  const { parentPort } = require('worker_threads');
  let text = '';
  process.stdin.setEncoding('utf8');
  process.stdin.on('data', d => { text += d; });
  process.stdin.on('end', () => parentPort.postMessage(text));
`;

const results = {};
const done = (name, value) => {
  results[name] = value;
  if (Object.keys(results).length < 4) return;
  assert.deepEqual(results, {
    piped: 'hello world!',
    closed: '',
    echo: 'ping\n',
    idle: 0,
  });
  console.log('worker stdin: ok');
};

const piped = new Worker(reader, { eval: true, stdin: true });
assert.equal(typeof piped.stdin.write, 'function');
assert.equal(piped.stdin.writable, true);
piped.stdin.write('hello ');
piped.stdin.write(Buffer.from('world'));
setTimeout(() => piped.stdin.end('!'), 30);
piped.on('message', m => done('piped', m));

const closed = new Worker(reader, { eval: true });
assert.equal(closed.stdin, null);
closed.on('message', m => done('closed', m));

const echo = new Worker('process.stdin.pipe(process.stdout)', { eval: true, stdin: true, stdout: true });
let echoed = '';
echo.stdout.setEncoding('utf8');
echo.stdout.on('data', d => { echoed += d; });
echo.on('exit', () => done('echo', echoed));
echo.stdin.end('ping\n');

const idle = new Worker('1', { eval: true, stdin: true });
idle.on('exit', code => done('idle', code));
