import assert from "node:assert";
import { setInterval } from "node:timers/promises";

const sleep = (ms) => new Promise((r) => globalThis.setTimeout(r, ms));

{
  let n = 0;
  for await (const v of setInterval(2, "x")) {
    assert.equal(v, "x");
    if (++n === 3) break;
  }
  assert.equal(n, 3);
}

{
  const it = setInterval(2, 7);
  assert.equal(it[Symbol.asyncIterator](), it);
  const [a, b] = await Promise.all([it.next(), it.next()]);
  assert.deepStrictEqual([a, b], [{ value: 7, done: false }, { value: 7, done: false }]);
  assert.deepStrictEqual(await it.return("r"), { value: "r", done: true });
  assert.deepStrictEqual(await it.next(), { value: undefined, done: true });
}

{
  const it = setInterval(2, "v");
  await it.next();
  await sleep(20);
  assert.equal((await it.next()).value, "v");
  assert.equal((await it.next()).value, "v");
  await it.return();
}

{
  const ac = new AbortController();
  const it = setInterval(2, 1, { signal: ac.signal });
  await it.next();
  const pending = it.next();
  ac.abort();
  await assert.rejects(pending, { name: "AbortError" });
  assert.deepStrictEqual(await it.next(), { value: undefined, done: true });
}

{
  const ac = new AbortController();
  const it = setInterval(2, 1, { signal: ac.signal });
  await it.next();
  await sleep(20);
  ac.abort();
  let drained = 0;
  try {
    for (;;) {
      const r = await it.next();
      assert.equal(r.done, false);
      drained++;
    }
  } catch (e) {
    assert.equal(e.name, "AbortError");
  }
  assert.ok(drained > 0);
  assert.deepStrictEqual(await it.next(), { value: undefined, done: true });
}

{
  const ac = new AbortController();
  ac.abort("why");
  const it = setInterval(2, 1, { signal: ac.signal });
  await assert.rejects(it.next(), (e) => e.name === "AbortError" && e.cause === "why");
  assert.deepStrictEqual(await it.next(), { value: undefined, done: true });
}

for (const opts of [5, { signal: 1 }, { ref: "no" }]) {
  const it = setInterval(2, 1, opts);
  await assert.rejects(it.next(), { name: "TypeError", code: "ERR_INVALID_ARG_TYPE" });
}

{
  const it = setInterval(2, "w");
  await assert.rejects(it.throw(new Error("t")), /t/);
  assert.deepStrictEqual(await it.next(), { value: undefined, done: true });
}

{
  const it = setInterval(60000, "u", { ref: false });
  it.next().then(() => assert.fail("unref interval kept the loop alive"));
}

console.log("ok");
