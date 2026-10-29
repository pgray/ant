const assert = require('node:assert');
const { EventEmitter } = require('node:events');
const { Readable } = require('node:stream');

const N = 3000;

class Base {
  constructor(x) {
    this.x = x;
  }
  sum() {
    return this.x + this.y;
  }
}

class Plain extends Base {
  constructor(x, y) {
    super(x);
    this.y = y;
  }
}

class Forwarded extends Base {}

const swapped = { swapped: true };
class Swapper {
  constructor(swap) {
    if (swap) return swapped;
    this.own = true;
  }
}

class Rebound extends Swapper {
  constructor(swap, v) {
    super(swap);
    this.v = v;
  }
}

class Branchy extends Base {
  constructor(flag) {
    if (flag) super(1);
    else super(2);
    this.y = flag ? 10 : 20;
  }
}

class Explicit extends Base {
  constructor(kind) {
    super(0);
    if (kind === 1) return { replaced: true };
    if (kind === 2) return undefined;
    if (kind === 3) return 42;
  }
}

class Target extends Base {
  constructor() {
    super(new.target.name);
  }
}

class Arrow extends Base {
  constructor(x) {
    super(x);
    this.get = () => this.x;
  }
}

class Emitter extends EventEmitter {
  constructor(tag) {
    super();
    this.tag = tag;
  }
}

class Source extends Readable {
  constructor(items) {
    super();
    this.items = items;
  }
  _read() {
    this.push(this.items.length ? this.items.shift() : null);
  }
}

class Unstable extends Swapper {
  constructor(swap, v) {
    super(swap);
    this.v = v * 2;
  }
}

for (let i = 0; i < N; i++) {
  const p = new Plain(i, 1);
  assert.strictEqual(p.sum(), i + 1);
  assert.ok(p instanceof Plain && p instanceof Base);

  const f = new Forwarded(i);
  assert.strictEqual(f.x, i);

  const swap = i % 7 === 0;
  const r = new Rebound(swap, i);
  if (swap) {
    assert.strictEqual(r, swapped);
    assert.strictEqual(r.v, i);
  } else {
    assert.ok(r instanceof Rebound);
    assert.strictEqual(r.own, true);
    assert.strictEqual(r.v, i);
  }

  const b = new Branchy(i % 2 === 0);
  assert.strictEqual(b.sum(), i % 2 === 0 ? 11 : 22);

  assert.deepStrictEqual(new Explicit(1), { replaced: true });
  assert.ok(new Explicit(2) instanceof Explicit);
  assert.ok(new Explicit(0) instanceof Explicit);
  assert.throws(() => new Explicit(3), TypeError);

  assert.strictEqual(new Target().x, 'Target');
  assert.strictEqual(new Arrow(i).get(), i);

  const e = new Emitter(i);
  let seen = 0;
  e.on('ping', v => { seen = v; });
  e.emit('ping', i);
  assert.strictEqual(seen, i);
  assert.strictEqual(e.tag, i);
}

for (let i = 0; i < N; i++) {
  const swap = i > N - 50;
  const u = new Unstable(swap, i < N - 10 ? i : 1.5);
  if (swap) assert.strictEqual(u, swapped);
  else assert.strictEqual(u.v, i * 2);
}
assert.strictEqual(swapped.v, 3);

class Guarded extends Base {
  constructor(kind) {
    super(kind);
    if (kind === 1) {
      try { return 's'; } catch (e) { return { caught: true }; }
    }
    if (kind === 2) {
      try { return 7; } finally { return { fin: true }; }
    }
    if (kind === 3) {
      try { this.y = 1; } finally { this.z = 2; }
    }
  }
}

for (let i = 0; i < N; i++) {
  assert.throws(() => new Guarded(1), TypeError);
  assert.deepStrictEqual(new Guarded(2), { fin: true });
  const g = new Guarded(3);
  assert.ok(g instanceof Guarded);
  assert.strictEqual(g.y + g.z, 3);
}

const table = [10, 20, 30, 40];
class Exiting extends Swapper {
  constructor(swap, i) {
    super(swap);
    this.k = table[i] + i;
  }
}
for (let i = 0; i < N; i++) new Exiting(i % 3 === 0, i & 3);
const exitedSwap = new Exiting(true, 'x');
assert.strictEqual(exitedSwap, swapped);
assert.strictEqual(exitedSwap.k, 'undefinedx');
const exitedOwn = new Exiting(false, 'y');
assert.ok(exitedOwn instanceof Exiting);
assert.strictEqual(exitedOwn.k, 'undefinedy');

const chunks = [];
const s = new Source(['a', 'b', 'c']);
s.on('data', d => chunks.push(String(d)));
s.on('end', () => {
  assert.deepStrictEqual(chunks, ['a', 'b', 'c']);
  console.log('ok');
});
