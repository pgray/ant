const assert = require('node:assert');
const dns = require('node:dns');
const dnsPromises = require('node:dns/promises');

assert.strictEqual(dnsPromises, dns.promises);
assert.strictEqual(require('dns/promises'), dnsPromises);
assert.strictEqual(dns.getDefaultResultOrder(), 'verbatim');
assert.strictEqual(dns.NOTFOUND, 'ENOTFOUND');
assert.strictEqual(dnsPromises.CANCELLED, 'ECANCELLED');
for (const name of ['ADDRCONFIG', 'V4MAPPED', 'ALL']) assert.strictEqual(typeof dns[name], 'number', name);
for (const name of ['lookup', 'lookupService', 'resolve', 'resolve4', 'resolveTxt', 'reverse', 'getServers', 'setServers', 'Resolver'])
  assert.strictEqual(typeof dns[name], 'function', name);

const code = (fn, expected) => assert.throws(fn, e => e.code === expected, expected);
code(() => dns.lookup('x'), 'ERR_INVALID_ARG_TYPE');
code(() => dns.lookup(5, () => {}), 'ERR_INVALID_ARG_TYPE');
code(() => dns.lookup('x', 'y', () => {}), 'ERR_INVALID_ARG_TYPE');
code(() => dns.lookup('x', { family: 5 }, () => {}), 'ERR_INVALID_ARG_VALUE');
code(() => dns.lookup('x', { order: 'nope' }, () => {}), 'ERR_INVALID_ARG_VALUE');
code(() => dns.lookup('x', { hints: 99999 }, () => {}), 'ERR_INVALID_ARG_VALUE');
code(() => dns.lookupService('nope', 80, () => {}), 'ERR_INVALID_ARG_VALUE');
code(() => dns.lookupService('127.0.0.1', 99999, () => {}), 'ERR_SOCKET_BAD_PORT');
code(() => dns.setDefaultResultOrder('x'), 'ERR_INVALID_ARG_VALUE');
code(() => dns.setServers(['not-ip']), 'ERR_INVALID_IP_ADDRESS');
code(() => dnsPromises.lookup(42), 'ERR_INVALID_ARG_TYPE');
code(() => dns.resolve('x', 'BOGUS', () => {}), 'ERR_INVALID_ARG_VALUE');
assert.throws(() => dns.Resolver(), TypeError);

(async () => {
  assert.deepStrictEqual(await dnsPromises.lookup('127.0.0.1'), { address: '127.0.0.1', family: 4 });
  assert.deepStrictEqual(await dnsPromises.lookup('::1', { all: true }), [{ address: '::1', family: 6 }]);
  assert.deepStrictEqual(await dnsPromises.lookup(), { address: null, family: 4 });

  const all = await dnsPromises.lookup('localhost', { all: true });
  assert.ok(all.length > 0 && all.every(a => (a.family === 4 && a.address === '127.0.0.1') || (a.family === 6 && a.address === '::1')), JSON.stringify(all));
  assert.deepStrictEqual(await dnsPromises.lookup('localhost', 4), { address: '127.0.0.1', family: 4 });
  const v4first = await dnsPromises.lookup('localhost', { all: true, order: 'ipv4first' });
  assert.strictEqual(v4first[0].family, 4);

  const missing = await dnsPromises.lookup('definitely-not-real.invalid').catch(e => e);
  assert.strictEqual(missing.code, 'ENOTFOUND');
  assert.strictEqual(missing.syscall, 'getaddrinfo');
  assert.strictEqual(missing.hostname, 'definitely-not-real.invalid');
  assert.strictEqual(missing.message, 'getaddrinfo ENOTFOUND definitely-not-real.invalid');
  assert.deepStrictEqual(Object.keys(missing), ['errno', 'code', 'syscall', 'hostname']);

  const reverse = await dnsPromises.reverse('not-an-ip').catch(e => e);
  assert.strictEqual(reverse.code, 'EINVAL');
  assert.strictEqual(reverse.syscall, 'getHostByAddr');

  const service = await dnsPromises.lookupService('127.0.0.1', 0).catch(e => e);
  assert.ok(typeof service.hostname === 'string' || service.code, JSON.stringify(service));

  await new Promise(resolve => {
    let sync = true;
    dns.lookup('127.0.0.1', (err, address, family) => {
      assert.strictEqual(sync, false);
      assert.deepStrictEqual([err, address, family], [null, '127.0.0.1', 4]);
      dns.lookup('localhost', { all: true }, (err2, list) => {
        assert.strictEqual(err2, null);
        assert.ok(Array.isArray(list) && list.length > 0);
        resolve();
      });
    });
    sync = false;
  });

  dns.setDefaultResultOrder('ipv4first');
  assert.strictEqual(dns.getDefaultResultOrder(), 'ipv4first');
  assert.strictEqual((await dnsPromises.lookup('localhost', { all: true }))[0].family, 4);
  dns.setDefaultResultOrder('verbatim');

  console.log('dns lookup OK');
})();
