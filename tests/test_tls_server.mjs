import assert from 'node:assert';
import fs from 'node:fs';
import https from 'node:https';
import net from 'node:net';
import tls from 'node:tls';

const fixture = name => fs.readFileSync(new URL(`./fixtures/tls/${name}`, import.meta.url));
const ca = fixture('ca-cert.pem');
const serverOptions = { key: fixture('server-key.pem'), cert: fixture('server-cert.pem') };
const clientOptions = { host: '127.0.0.1', servername: 'localhost', ca };

const once = (emitter, event) => new Promise(resolve => emitter.once(event, (...args) => resolve(args)));
const listen = async server => {
  server.listen(0, '127.0.0.1');
  await once(server, 'listening');
  return server.address().port;
};
const readAll = socket => new Promise((resolve, reject) => {
  let text = '';
  socket.on('data', chunk => (text += chunk));
  socket.on('error', reject);
  socket.on('close', () => resolve(text));
});

// echo with ALPN negotiation and server-side events
{
  const server = tls.createServer({ ...serverOptions, ALPNProtocols: ['h2', 'http/1.1'] }, socket => {
    assert.strictEqual(socket.alpnProtocol, 'http/1.1');
    assert.strictEqual(socket.remoteAddress, '127.0.0.1');
    socket.once('data', data => socket.end(`echo:${data}`));
  });
  assert(server instanceof tls.Server);
  assert(server instanceof net.Server);
  const port = await listen(server);
  assert.strictEqual(server.address().port, port);

  const client = tls.connect({ ...clientOptions, port, ALPNProtocols: ['http/1.1'] }, () => client.write('hi'));
  assert.strictEqual(await readAll(client), 'echo:hi');
  assert.strictEqual(client.alpnProtocol, 'http/1.1');

  // a client that does not trust the server's CA fails, and the server reports it
  const clientError = once(server, 'tlsClientError');
  const untrusted = tls.connect({ host: '127.0.0.1', servername: 'localhost', port });
  const [err] = await once(untrusted, 'error');
  assert.match(err.message, /CERTIFICATE_VERIFY_FAILED/);
  await clientError;

  server.close();
  await once(server, 'close');
}

// client certificates are required with requestCert
{
  const server = tls.createServer({ ...serverOptions, ca, requestCert: true }, socket => {
    assert.strictEqual(socket.authorized, true);
    socket.end('ok');
  });
  const port = await listen(server);

  const good = tls.connect({ ...clientOptions, port, key: fixture('client-key.pem'), cert: fixture('client-cert.pem') });
  assert.strictEqual(await readAll(good), 'ok');

  const rejected = once(server, 'tlsClientError');
  const anonymous = tls.connect({ ...clientOptions, port });
  anonymous.on('error', () => {});
  const [err] = await rejected;
  assert.match(err.message, /CERTIFICATE/);

  server.close();
  await once(server, 'close');
}

// https.createServer serves HTTP over the TLS listener
{
  const server = https.createServer(serverOptions, (req, res) => res.end(`${req.method} ${req.url}`));
  const port = await listen(server);
  const client = tls.connect({ ...clientOptions, port }, () => {
    client.write('GET /path HTTP/1.1\r\nHost: localhost\r\nConnection: close\r\n\r\n');
  });
  const response = await readAll(client);
  assert.match(response, /^HTTP\/1\.1 200/);
  assert.match(response, /GET \/path$/);
  server.close();
  await once(server, 'close');
}

// destroy() is a no-op once a server-side socket closed, whatever the peer
// did, flips destroyed at once, and never emits a second 'close'
for (const [module, options] of [[net, {}], [tls, serverOptions]]) {
  for (const action of ['end', 'destroy', 'none']) {
    let accepted;
    let closes = 0;
    const server = module.createServer(options, socket => {
      accepted = socket;
      socket.on('error', () => {});
      socket.on('close', () => closes++);
    });
    const port = await listen(server);
    const client = module.connect({ ...(module === tls ? clientOptions : { host: '127.0.0.1' }), port });
    client.on('error', () => {});
    await once(client, module === tls ? 'secureConnect' : 'connect');
    while (!accepted) await new Promise(resolve => setTimeout(resolve, 5));

    if (action !== 'none') {
      client[action]();
      await once(accepted, 'close');
    }
    assert.strictEqual(accepted.destroy(), accepted, `${action}: first destroy`);
    assert.strictEqual(accepted.destroy(), accepted, `${action}: second destroy`);
    assert.strictEqual(accepted.destroyed, true, `${action}: destroyed`);

    client.destroy();
    await new Promise(resolve => setTimeout(resolve, 100));
    assert.strictEqual(closes, 1, `${action}: close events`);
    server.close();
  }
}

// checkServerIdentity follows Node's matching rules
assert.strictEqual(tls.checkServerIdentity('a.example.com', { subjectaltname: 'DNS:*.example.com' }), undefined);
assert.strictEqual(tls.checkServerIdentity('A.Example.COM.', { subjectaltname: 'DNS:a.example.com' }), undefined);
assert.strictEqual(tls.checkServerIdentity('::1', { subjectaltname: 'IP Address:0:0:0:0:0:0:0:1' }), undefined);
assert.strictEqual(tls.checkServerIdentity('localhost', { subject: { CN: 'localhost' } }), undefined);
for (const [host, cert] of [
  ['a.b.example.com', { subjectaltname: 'DNS:*.example.com' }],
  ['example.com', { subjectaltname: 'DNS:*.com' }],
  ['localhost', { subjectaltname: 'DNS:other', subject: { CN: 'localhost' } }],
  ['127.0.0.1', { subjectaltname: 'DNS:127.0.0.1' }],
  ['localhost', {}]
]) {
  const err = tls.checkServerIdentity(host, cert);
  assert(err instanceof Error, `${host} should not match ${JSON.stringify(cert)}`);
  assert.strictEqual(err.code, 'ERR_TLS_CERT_ALTNAME_INVALID');
  assert.strictEqual(err.host, host);
}

const ciphers = tls.getCiphers();
assert(ciphers.includes('tls_aes_128_gcm_sha256'));
assert(ciphers.includes('ecdhe-rsa-aes128-gcm-sha256'));
assert.deepStrictEqual(ciphers, [...new Set(ciphers)].sort());

assert(tls.rootCertificates.length > 0);
assert(Object.isFrozen(tls.rootCertificates));
assert.strictEqual(tls.rootCertificates, tls.rootCertificates);
assert.match(tls.rootCertificates[0], /^-----BEGIN CERTIFICATE-----\n[\s\S]+\n-----END CERTIFICATE-----$/);

console.log('tls server tests passed');
