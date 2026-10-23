const assert = require('node:assert');
const http = require('node:http');

const seen = [];

function listen(handler) {
  const server = http.createServer(handler);
  return new Promise(resolve => server.listen(0, '127.0.0.1', () => resolve(server)));
}

function echo(req, res) {
  let body = '';
  req.on('data', chunk => (body += chunk));
  req.on('end', () => {
    seen.push({ url: req.url, method: req.method, headers: req.headers, body });
    res.writeHead(200, { 'content-type': 'text/plain' });
    res.end('ok');
  });
}

const credentials = {
  authorization: 'Bearer npm_secret',
  'proxy-authorization': 'Basic cHJveHk6c2VjcmV0',
  cookie: 'session=secret',
  'x-keep': 'kept',
};

async function main() {
  const other = await listen(echo);
  const otherBase = `http://127.0.0.1:${other.address().port}`;

  const origin = await listen((req, res) => {
    const target = {
      '/cross': [302, `${otherBase}/landed`],
      '/same': [307, '/landed'],
      '/see-other': [303, `${otherBase}/landed`],
      '/data': [302, 'data:text/plain,hello'],
      '/blob': [302, 'blob:nodedata:00000000-0000-0000-0000-000000000000'],
    }[req.url];
    if (!target) return echo(req, res);
    req.resume();
    res.writeHead(target[0], { location: target[1] });
    res.end();
  });
  const base = `http://127.0.0.1:${origin.address().port}`;

  try {
    seen.length = 0;
    assert.strictEqual(await (await fetch(`${base}/cross`, { headers: credentials })).text(), 'ok');
    assert.strictEqual(seen.length, 1);
    assert.strictEqual(seen[0].headers.authorization, undefined);
    assert.strictEqual(seen[0].headers['proxy-authorization'], undefined);
    assert.strictEqual(seen[0].headers.cookie, undefined);
    assert.strictEqual(seen[0].headers['x-keep'], 'kept');

    seen.length = 0;
    await (await fetch(`${base}/same`, { method: 'POST', body: 'payload', headers: credentials })).text();
    assert.strictEqual(seen[0].method, 'POST');
    assert.strictEqual(seen[0].body, 'payload');
    assert.strictEqual(seen[0].headers.authorization, credentials.authorization);
    assert.strictEqual(seen[0].headers.cookie, credentials.cookie);

    seen.length = 0;
    await (await fetch(`${base}/see-other`, {
      method: 'POST',
      body: 'payload',
      headers: { ...credentials, 'content-type': 'text/plain', 'content-language': 'en' },
    })).text();
    assert.strictEqual(seen[0].method, 'GET');
    assert.strictEqual(seen[0].body, '');
    assert.strictEqual(seen[0].headers['content-type'], undefined);
    assert.strictEqual(seen[0].headers['content-language'], undefined);
    assert.strictEqual(seen[0].headers.authorization, undefined);
    assert.strictEqual(seen[0].headers['x-keep'], 'kept');

    const callerHeaders = new Headers(credentials);
    await (await fetch(`${base}/cross`, { headers: callerHeaders })).text();
    assert.strictEqual(callerHeaders.get('authorization'), credentials.authorization);
    assert.strictEqual(callerHeaders.get('cookie'), credentials.cookie);

    const callerRequest = new Request(`${base}/cross`, { headers: credentials });
    await (await fetch(callerRequest)).text();
    assert.strictEqual(callerRequest.headers.get('authorization'), credentials.authorization);
    assert.strictEqual(callerRequest.url, `${base}/cross`);

    await assert.rejects(fetch(`${base}/data`), TypeError);
    await assert.rejects(fetch(`${base}/blob`), TypeError);

    console.log('ok');
  } finally {
    origin.close();
    other.close();
  }
}

main().catch(error => {
  process.exitCode = 1;
  console.error(error);
});
