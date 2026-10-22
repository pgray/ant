const assert = require('node:assert');
const tls = require('node:tls');

function rejects(host, cert, reason) {
  const err = tls.checkServerIdentity(host, cert);
  assert.ok(err, `${JSON.stringify(host)} should be rejected`);
  assert.strictEqual(err.code, 'ERR_TLS_CERT_ALTNAME_INVALID');
  if (reason) assert.strictEqual(err.reason, reason);
}

function accepts(host, cert) {
  assert.strictEqual(tls.checkServerIdentity(host, cert), undefined, `${JSON.stringify(host)} should be accepted`);
}

for (const [host, normalized] of [
  ['0127.0.0.1', '87.0.0.1'],
  ['0177.0.0.1', '127.0.0.1'],
  ['0x7f.0.0.1', '127.0.0.1'],
  ['2130706433', '127.0.0.1'],
  ['127.1', '127.0.0.1'],
  ['0x7f000001', '127.0.0.1'],
  ['127.0.1', '127.0.0.1'],
]) {
  rejects(host, { subjectaltname: `DNS:${normalized}`, subject: {} }, 'Host is a non-canonical IP address');
  rejects(host, { subjectaltname: `IP Address:${normalized}`, subject: {} }, 'Host is a non-canonical IP address');
  rejects(host, { subjectaltname: 'IP Address:127.0.0.1', subject: { CN: normalized } }, 'Host is a non-canonical IP address');
}

rejects('0127.0.0.1', { subjectaltname: 'IP Address:127.0.0.1', subject: {} }, 'Host is a non-canonical IP address');

rejects('１２７.0.0.1', { subjectaltname: 'DNS:127.0.0.1', subject: {} });
rejects('１２７.0.0.1', { subject: { CN: '127.0.0.1' } });
rejects('127。0.0.1', { subjectaltname: 'DNS:127.0.0.1', subject: {} });

accepts('127.0.0.1', { subjectaltname: 'IP Address:127.0.0.1', subject: {} });
accepts('10.0.0.1', { subjectaltname: 'IP Address:10.0.0.1', subject: {} });
accepts('0.0.0.0', { subjectaltname: 'IP Address:0.0.0.0', subject: {} });
accepts('::1', { subjectaltname: 'IP Address:::1', subject: {} });
accepts('2001:db8::1', { subjectaltname: 'IP Address:2001:DB8:0:0:0:0:0:1', subject: {} });
rejects('::1', { subjectaltname: 'IP Address:::2', subject: {} }, "IP: ::1 is not in the cert's list: ::2");
rejects('example.com', { subjectaltname: 'DNS:', subject: { CN: 'example.com' } });
accepts('example.com', { subjectaltname: 'DNS:example.com', subject: {} });
accepts('0x.example.com', { subjectaltname: 'DNS:0x.example.com', subject: {} });
accepts('123.example.com', { subjectaltname: 'DNS:*.example.com', subject: {} });

console.log('ok');
