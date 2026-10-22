const assert = require('node:assert');
const { Readable } = require('node:stream');

function sync() {
  const events = [];
  const stream = new Readable({ read() {} });
  stream.push('a');
  stream.push('b');
  stream.push(null);

  let text = '';
  stream.on('readable', () => {
    events.push('readable');
    let chunk;
    while ((chunk = stream.read()) !== null) text += chunk;
  });

  return new Promise(resolve => {
    stream.on('end', () => {
      events.push('end');
      resolve({ text, events });
    });
  });
}

function delayed() {
  const parts = ['one', 'two', 'three'];
  const stream = new Readable({
    read() {
      setTimeout(() => this.push(parts.length ? parts.shift() : null), 1);
    },
  });

  let text = '';
  let readable = 0;
  stream.on('readable', () => {
    readable++;
    let chunk;
    while ((chunk = stream.read()) !== null) text += chunk;
  });

  return new Promise(resolve => stream.on('end', () => resolve({ text, readable })));
}

function lateListener() {
  const stream = new Readable({ read() {} });
  stream.push('late');
  stream.push(null);

  return new Promise(resolve => {
    setTimeout(() => {
      let text = '';
      stream.on('readable', () => {
        let chunk;
        while ((chunk = stream.read()) !== null) text += chunk;
      });
      stream.on('end', () => resolve(text));
    }, 5);
  });
}

let finished = false;
process.on('exit', () => {
  if (!finished) {
    console.error('readable streams never ended');
    process.exit(1);
  }
});

(async () => {
  const first = await sync();
  assert.strictEqual(first.text, 'ab');
  assert.strictEqual(first.events.at(-1), 'end');
  assert.ok(first.events.includes('readable'));

  const second = await delayed();
  assert.strictEqual(second.text, 'onetwothree');
  assert.ok(second.readable >= 1);

  assert.strictEqual(await lateListener(), 'late');
  finished = true;
  console.log('ok');
})();
