const assert = require('node:assert');
const path = require('node:path');
const dns = require('node:dns');
const { spawn, spawnSync } = require('node:child_process');

if (spawnSync('python3', ['--version']).status !== 0) {
  console.log('dns resolver skipped: python3 is not available');
  process.exit(0);
}

const fixture = path.join(__dirname, 'fixtures', 'dns_server.py');
const server = spawn('python3', [fixture], { stdio: ['pipe', 'pipe', 'inherit'] });

const SOA = { nsname: 'ns1.fixture.test', hostmaster: 'hostmaster.fixture.test', serial: 2024010101, refresh: 3600, retry: 600, expire: 86400, minttl: 300 };
const NAPTR = { flags: 'S', service: 'SIP+D2U', regexp: '', replacement: '_sip._udp.fixture.test', order: 100, preference: 10 };

async function expectError(promise, code, syscall, hostname) {
  const error = await promise.then(() => null, e => e);
  assert.ok(error, `expected ${code}`);
  assert.strictEqual(error.code, code);
  assert.strictEqual(error.syscall, syscall);
  assert.strictEqual(error.hostname, hostname);
  assert.strictEqual(error.message, `${syscall} ${code} ${hostname}`);
  assert.deepStrictEqual(Object.keys(error), ['errno', 'code', 'syscall', 'hostname']);
}

server.stdout.once('data', async chunk => {
  const address = `127.0.0.1:${String(chunk).trim()}`;
  try {
    const r = new dns.promises.Resolver({ timeout: 300, tries: 1 });
    r.setServers([address]);
    assert.deepStrictEqual(r.getServers(), [address]);

    assert.deepStrictEqual(await r.resolve4('fixture.test'), ['192.0.2.1', '192.0.2.2']);
    assert.deepStrictEqual(await r.resolve4('fixture.test', { ttl: true }), [{ address: '192.0.2.1', ttl: 300 }, { address: '192.0.2.2', ttl: 300 }]);
    assert.deepStrictEqual(await r.resolve6('fixture.test'), ['2001:db8::1']);
    assert.deepStrictEqual(await r.resolveMx('fixture.test'), [{ exchange: 'mail.fixture.test', priority: 10 }]);
    assert.deepStrictEqual(await r.resolveTxt('fixture.test'), [['v=spf1', '-all'], ['second']]);
    assert.deepStrictEqual(await r.resolveNs('fixture.test'), ['ns1.fixture.test']);
    assert.deepStrictEqual(await r.resolveSoa('fixture.test'), SOA);
    assert.deepStrictEqual(await r.resolveCaa('fixture.test'), [{ critical: 0, issue: 'letsencrypt.org' }]);
    assert.deepStrictEqual(await r.resolveNaptr('fixture.test'), [NAPTR]);
    const tlsa = await r.resolveTlsa('fixture.test');
    assert.deepStrictEqual([...new Uint8Array(tlsa[0].data)], [0xde, 0xad, 0xbe, 0xef]);
    assert.deepStrictEqual(Object.keys(tlsa[0]), ['certUsage', 'selector', 'match', 'data']);
    assert.deepStrictEqual(await r.resolveSrv('_sip._tcp.fixture.test'), [{ name: 'sip.fixture.test', port: 5060, priority: 0, weight: 5 }]);
    assert.deepStrictEqual(Object.keys((await r.resolveSrv('_sip._tcp.fixture.test'))[0]), ['name', 'port', 'priority', 'weight']);
    assert.deepStrictEqual(await r.resolveCname('alias.fixture.test'), ['fixture.test']);
    assert.deepStrictEqual(await r.resolvePtr('1.2.0.192.in-addr.arpa'), ['host.fixture.test']);
    assert.deepStrictEqual(await r.resolve('fixture.test', 'SOA'), SOA);
    assert.deepStrictEqual(await r.resolve('fixture.test'), ['192.0.2.1', '192.0.2.2']);
    assert.deepStrictEqual(await r.reverse('192.0.2.1'), ['host.fixture.test']);

    const any = await r.resolveAny('fixture.test');
    assert.deepStrictEqual(any.map(record => record.type), ['A', 'A', 'AAAA', 'MX', 'NS', 'TXT', 'TXT', 'NAPTR', 'SOA', 'TLSA', 'CAA']);
    assert.deepStrictEqual(any[5], { entries: ['v=spf1', '-all'], type: 'TXT' });

    await expectError(r.resolve4('missing.fixture.test'), 'ENOTFOUND', 'queryA', 'missing.fixture.test');
    await expectError(r.resolveMx('empty.fixture.test'), 'ENODATA', 'queryMx', 'empty.fixture.test');
    await expectError(r.resolve4('slow.fixture.test'), 'ETIMEOUT', 'queryA', 'slow.fixture.test');

    const cancelled = r.resolve4('slow.fixture.test');
    r.cancel();
    await expectError(cancelled, 'ECANCELLED', 'queryA', 'slow.fixture.test');

    dns.setServers([address, '[::1]:5353', '8.8.8.8']);
    assert.deepStrictEqual(dns.getServers(), [address, '[::1]:5353', '8.8.8.8']);
    dns.setServers([address]);

    await new Promise((resolve, reject) => {
      let sync = true;
      dns.resolveTxt('fixture.test', (err, records) => {
        try {
          assert.strictEqual(sync, false);
          assert.strictEqual(err, null);
          assert.deepStrictEqual(records, [['v=spf1', '-all'], ['second']]);
          const cb = new dns.Resolver({ timeout: 300, tries: 1 });
          cb.setServers([address]);
          cb.resolveMx('missing.fixture.test', err2 => {
            try { assert.strictEqual(err2.code, 'ENOTFOUND'); resolve(); } catch (e) { reject(e); }
          });
        } catch (e) { reject(e); }
      });
      sync = false;
    });

    console.log('dns resolver OK');
  } finally {
    server.stdin.end();
  }
});
