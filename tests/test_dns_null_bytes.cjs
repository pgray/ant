const assert = require('node:assert');
const dns = require('node:dns');
const net = require('node:net');
const tls = require('node:tls');

const name = 'example.com\0.trusted.corp';
const ip = '127.0.0.1\0.trusted.corp';
const err = argument => ({
  code: 'ERR_INVALID_ARG_VALUE',
  name: 'TypeError',
  message: new RegExp(`^The argument '${argument}' must be a string without null bytes\\. Received `),
});

const methods = [
  'resolve', 'resolve4', 'resolve6', 'resolveAny', 'resolveCaa', 'resolveCname', 'resolveMx',
  'resolveNaptr', 'resolveNs', 'resolvePtr', 'resolveSoa', 'resolveSrv', 'resolveTxt',
];
const rrtypes = ['A', 'AAAA', 'ANY', 'CAA', 'CNAME', 'MX', 'NAPTR', 'NS', 'PTR', 'SOA', 'SRV', 'TXT'];
const never = () => assert.fail('callback must not run');

for (const resolver of [dns, new dns.Resolver()]) {
  for (const method of methods) assert.throws(() => resolver[method](name, never), err('name'));
  for (const rrtype of rrtypes) assert.throws(() => resolver.resolve(name, rrtype, never), err('name'));
  assert.throws(() => resolver.reverse(ip, never), err('name'));
}

for (const resolver of [dns.promises, new dns.promises.Resolver()]) {
  for (const method of methods) assert.throws(() => resolver[method](name), err('name'));
  for (const rrtype of rrtypes) assert.throws(() => resolver.resolve(name, rrtype), err('name'));
  assert.throws(() => resolver.reverse(ip), err('name'));
}

for (const resolver of [new dns.Resolver(), new dns.promises.Resolver()]) {
  assert.throws(() => resolver.setLocalAddress('127.0.0.1\0junk'), err('ipv4'));
}

assert.throws(() => dns.lookup(name, never), err('hostname'));
assert.throws(() => dns.promises.lookup(name), err('hostname'));
assert.throws(() => net.connect({ host: name, port: 9 }), err('host'));
assert.throws(() => net.connect(9, name), err('host'));
assert.throws(() => net.connect({ path: '/tmp/sock\0.evil' }), err('path'));
assert.throws(() => tls.connect({ host: name, port: 443 }), err('host'));
assert.throws(() => tls.connect({ host: 'example.com', servername: name, port: 443 }), err('servername'));

console.log('ok');
