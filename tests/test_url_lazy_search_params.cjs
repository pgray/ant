const assert = require('node:assert');

const out = [];
const a = new URL('https://example.com/p?x=1&y=2#h');
a.search = '?z=3';
out.push(a.searchParams.get('z'), String(a.searchParams.get('x')), a.href);
assert.strictEqual(a.searchParams, a.searchParams);

const b = new URL('https://example.com/');
const sp = b.searchParams;
sp.append('k', 'v w');
out.push(b.href, b.search);
b.href = 'https://other.test/q?m=5';
out.push(sp.get('m'), String(sp.get('k')), b.searchParams === sp);
b.searchParams.set('m', '6');
out.push(b.href);

const c = new URL('rel?q=%20a+b', 'https://base.test/dir/');
out.push(c.href, c.searchParams.get('q'), c.toString(), String(c), c.pathname, c.origin);
c.hash = '#frag';
c.pathname = '/new path';
out.push(c.href, JSON.stringify(c));

const d = new URL('https://u:p@host.test:8080/a/b?c=d#e');
out.push(d.username, d.password, d.host, d.hostname, d.port, d.pathname, d.search, d.hash, d.href);

out.push(URL.canParse('https://ok.test'), URL.canParse('not a url'), URL.canParse('/x', 'https://b.test'), URL.canParse('/x'));
out.push(URL.canParse('https://\uD800.test'), new URL('https://example.com/\uD800').pathname);

const empty = new URL('https://e.test/?#');
out.push(empty.search, empty.hash, empty.href, [...empty.searchParams].length);

let n = 0;
for (let i = 0; i < 20000; i++) n += new URL('/p' + (i & 7) + '?a=' + i, 'https://base.test/').href.length;
out.push(n);

const expected = ["3", "null", "https://example.com/p?z=3#h", "https://example.com/?k=v+w", "?k=v+w", "5", "null", "true", "https://other.test/q?m=6", "https://base.test/dir/rel?q=%20a+b", " a b", "https://base.test/dir/rel?q=%20a+b", "https://base.test/dir/rel?q=%20a+b", "/dir/rel", "https://base.test", "https://base.test/new%20path?q=%20a+b#frag", "\"https://base.test/new%20path?q=%20a+b#frag\"", "u", "p", "host.test:8080", "host.test", "8080", "/a/b", "?c=d", "#e", "https://u:p@host.test:8080/a/b?c=d#e", "true", "false", "true", "false", "false", "/%EF%BF%BD", "", "", "https://e.test/?#", "0", "548890"];
assert.deepStrictEqual(out.map(String), expected);
console.log('ok');
