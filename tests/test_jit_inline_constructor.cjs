const assert = require('node:assert');

const N = 20000;

class P {
  constructor(x, y) {
    this.x = x;
    this.y = y;
  }
  add(o) {
    return new P(this.x + o.x, this.y + o.y);
  }
}

function sumPoints(n) {
  let p = new P(0, 0);
  const one = new P(1, 2);
  for (let i = 0; i < n; i++) p = p.add(one);
  return p;
}

const summed = sumPoints(N);
assert.strictEqual(summed.x, N);
assert.strictEqual(summed.y, 2 * N);
assert.ok(summed instanceof P);
assert.strictEqual(Object.getPrototypeOf(summed), P.prototype);
assert.deepStrictEqual(Object.keys(summed), ['x', 'y']);

class Target {
  constructor() {
    this.target = new.target;
  }
}

function makeTargets(n) {
  let last;
  for (let i = 0; i < n; i++) last = new Target();
  return last;
}
assert.strictEqual(makeTargets(N).target, Target);

const returned = { tag: 'returned' };
class ReturnsObject {
  constructor(flag) {
    this.ignored = true;
    return flag ? returned : 7;
  }
}

function constructReturning(n) {
  let objects = 0;
  let instances = 0;
  for (let i = 0; i < n; i++) {
    const r = new ReturnsObject(i & 1);
    if (r === returned) objects++;
    else if (r instanceof ReturnsObject && r.ignored === true) instances++;
  }
  return [objects, instances];
}
assert.deepStrictEqual(constructReturning(N), [N / 2, N / 2]);

function Plain(a) {
  this.a = a;
}

function constructPlain(n) {
  let s = 0;
  let last;
  for (let i = 0; i < n; i++) {
    last = new Plain(i);
    s += last.a;
  }
  return [s, last];
}
const [plainSum, plainLast] = constructPlain(N);
assert.strictEqual(plainSum, (N * (N - 1)) / 2);
assert.strictEqual(Object.getPrototypeOf(plainLast), Plain.prototype);

function Swapped(v) {
  this.v = v;
}
const protoA = { kind: 'a' };
const protoB = { kind: 'b' };

function constructSwapped(n) {
  const kinds = { a: 0, b: 0 };
  for (let i = 0; i < n; i++) {
    Swapped.prototype = i < n / 2 ? protoA : protoB;
    kinds[new Swapped(i).kind]++;
  }
  return kinds;
}
assert.deepStrictEqual(constructSwapped(N), { a: N / 2, b: N / 2 });

class Other {
  constructor(x, y) {
    this.other = x - y;
  }
}

function constructVarying(n, ctors) {
  let s = 0;
  for (let i = 0; i < n; i++) {
    const C = ctors[i < n - 3 ? 0 : i % ctors.length];
    const o = new C(i, 1);
    s += o instanceof P ? o.x : o.other;
  }
  return s;
}
assert.strictEqual(constructVarying(N, [P, Other]), constructVarying(N, [P, Other]));

function factory(base) {
  return class {
    constructor(v) {
      this.v = v + base;
    }
  };
}
const Ten = factory(10);
const Hundred = factory(100);

function constructFactory(n) {
  let s = 0;
  for (let i = 0; i < n; i++) {
    const C = i & 1 ? Hundred : Ten;
    s += new C(1).v;
  }
  return s;
}
assert.strictEqual(constructFactory(N), (N / 2) * 11 + (N / 2) * 101);

class ReadsArg {
  constructor(o) {
    this.v = o.v;
  }
}

function constructThrowing(n) {
  let made = 0;
  for (let i = 0; i < n; i++) {
    new ReadsArg(i === n - 1 ? null : { v: i });
    made++;
  }
  return made;
}
assert.throws(() => constructThrowing(N), TypeError);

function constructCatching(n) {
  let caught = 0;
  for (let i = 0; i < n; i++) {
    try {
      new ReadsArg(i % 100 === 0 ? undefined : { v: i });
    } catch (e) {
      if (e instanceof TypeError) caught++;
    }
  }
  return caught;
}
assert.strictEqual(constructCatching(N), N / 100);

function callWithoutNew() {
  return P(1, 2);
}

function callClassHot(n) {
  let thrown = 0;
  for (let i = 0; i < n; i++) {
    try {
      callWithoutNew();
    } catch (e) {
      if (e instanceof TypeError) thrown++;
    }
  }
  return thrown;
}
assert.strictEqual(callClassHot(N), N);

function constructMixed(n) {
  const Bound = P.bind(null, 5);
  const Proxied = new Proxy(P, {});
  let s = 0;
  for (let i = 0; i < n; i++) {
    const C = i < n - 10 ? P : i & 1 ? Bound : Proxied;
    const o = new C(i, 3);
    s += o.x + o.y;
    assert.ok(o instanceof P);
  }
  return s;
}
const mixed = constructMixed(N);
let mixedExpected = 0;
for (let i = 0; i < N; i++) {
  if (i < N - 10 || !(i & 1)) mixedExpected += i + 3;
  else mixedExpected += 5 + i;
}
assert.strictEqual(mixed, mixedExpected);

class Base {
  constructor(x) {
    this.base = x;
  }
}
class Derived extends Base {
  constructor(x) {
    super(x);
    this.derived = x * 2;
  }
}

function constructDerived(n) {
  let s = 0;
  let last;
  for (let i = 0; i < n; i++) {
    last = new Derived(i);
    s += last.base + last.derived;
  }
  return [s, last];
}
const [derivedSum, derivedLast] = constructDerived(N);
assert.strictEqual(derivedSum, 3 * ((N * (N - 1)) / 2));
assert.ok(derivedLast instanceof Derived && derivedLast instanceof Base);

let sideEffects = 0;
class Effectful {
  constructor(v) {
    sideEffects++;
    this.v = v;
  }
}

function constructEffectful(n) {
  for (let i = 0; i < n; i++) new Effectful(i);
}
constructEffectful(N);
assert.strictEqual(sideEffects, N);

class Holder {
  constructor(i) {
    this.obj = { v: i };
    this.arr = [i, i + 1];
    this.str = 'v' + i;
    this.fn = () => i;
  }
}

function constructHolders(n) {
  const kept = [];
  for (let i = 0; i < n; i++) {
    const h = new Holder(i);
    if (i % 997 === 0) kept.push(h);
  }
  return kept;
}

const holders = constructHolders(200000);
const churn = [];
for (let i = 0; i < 200000; i++) churn.push({ i });
for (const h of holders) {
  const i = h.obj.v;
  assert.strictEqual(h.arr[0], i);
  assert.strictEqual(h.arr[1], i + 1);
  assert.strictEqual(h.str, 'v' + i);
  assert.strictEqual(h.fn(), i);
}

class Late {
  constructor(v) {
    this.v = v;
    this.w = v + 1;
  }
}

function constructLate(n) {
  let s = 0;
  for (let i = 0; i < n; i++) {
    const o = new Late(i);
    s += Object.prototype.hasOwnProperty.call(o, 'v') ? o.v : -1;
  }
  return s;
}
constructLate(N);
let setterRuns = 0;
Object.defineProperty(Late.prototype, 'v', {
  set(value) {
    setterRuns++;
    this._v = value;
  },
  get() {
    return this._v;
  },
  configurable: true,
});
assert.strictEqual(constructLate(100), -100);
assert.strictEqual(setterRuns, 100);
const late = new Late(5);
assert.strictEqual(late._v, 5);
assert.strictEqual(late.w, 6);
assert.ok(!Object.prototype.hasOwnProperty.call(late, 'v'));

delete Late.prototype.v;
Object.defineProperty(Late.prototype, 'w', { value: 0, writable: false, configurable: true });
function constructFrozen(n) {
  let thrown = 0;
  for (let i = 0; i < n; i++) {
    try {
      new Late(i);
    } catch (e) {
      if (e instanceof TypeError) thrown++;
    }
  }
  return thrown;
}
assert.strictEqual(constructFrozen(100), 100);
delete Late.prototype.w;
assert.strictEqual(constructLate(100), 4950);

const counter = { n: 0 };
class Counts {
  constructor(c) {
    c.n = c.n + 1;
    this.c = c;
  }
}

function constructCounts(n) {
  for (let i = 0; i < n; i++) new Counts(counter);
  return counter.n;
}
assert.strictEqual(constructCounts(N), N);

console.log('ok');
