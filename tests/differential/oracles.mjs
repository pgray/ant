// Property families with Node as the oracle: each case generates random
// inputs and records what the built-ins return for them. Error messages are
// engine-specific, so these probes record only error names (__recordName).

import { finish, int, literal, numberSource, pick, randomString, sample } from './common.mjs';

const numberPool = [
  0, -0, 1, -1, 0.1, 0.5, -1.5, 1 / 3, 2 ** 31 - 1, -(2 ** 31), 2 ** 32, 2 ** 53, 2 ** 53 + 2, -(2 ** 53) - 1,
  1e21, 1e-7, 123456789012345680000, 5e-324, 1.7976931348623157e308, 2.2250738585072014e-308, NaN, Infinity, -Infinity
];

function randomDouble(random) {
  const roll = random();
  if (roll < 0.4) return pick(random, numberPool);
  if (roll < 0.6) return int(random, -1e6, 1e6);
  const bits = new DataView(new ArrayBuffer(8));
  bits.setUint32(0, Math.floor(random() * 2 ** 32));
  bits.setUint32(4, Math.floor(random() * 2 ** 32));
  return bits.getFloat64(0);
}

const keyPool = ['a', 'b', '', '0', '1', '01', '-1', '1.5', '__proto__', 'constructor', 'toJSON', 'é', '😀', 'a\u0000b', '\uD800'];

// Source text for a random JSON-ish value; objects may use any key.
function valueSource(random, depth) {
  const roll = random();
  if (depth <= 0 || roll < 0.45) {
    const leaf = random();
    if (leaf < 0.35) return numberSource(randomDouble(random));
    if (leaf < 0.7) return literal(randomString(random, 6));
    return pick(random, ['true', 'false', 'null', 'undefined', '[]', '{}', '10n', 'new Date(0)', '() => 1', 'Symbol("s")']);
  }
  const count = int(random, 0, 4);
  if (roll < 0.7) return `[${Array.from({ length: count }, () => valueSource(random, depth - 1)).join(', ')}]`;
  const entries = Array.from({ length: count }, () => `${literal(pick(random, keyPool))}: ${valueSource(random, depth - 1)}`);
  return `{ ${entries.join(', ')} }`;
}

function jsonCase(random, index) {
  const values = Array.from({ length: int(random, 2, 5) }, () => ({
    source: valueSource(random, 3),
    edits: sample(random, [',', ']', '}', '"', '\\', '0', '-', 'e', ' ', ':', '\u2028', 'x', '{', '['], 3),
    positions: sample(random, [0, 0.1, 0.3, 0.5, 0.7, 0.9, 1], 3),
    indent: pick(random, [0, 1, 2, 4, 10, 11, '\t', '--', ''])
  }));
  return {
    family: 'json',
    id: `json-${index}`,
    shrinkKeys: ['values'],
    params: { values },
    build(params) {
      const body = params.values
        .map((value, i) => {
          const editsSource = value.edits
            .map((edit, k) => `t = t.slice(0, Math.floor(t.length * ${value.positions[k]})) + ${literal(edit)} + t.slice(Math.floor(t.length * ${value.positions[k]}) + ${k % 2});`)
            .join(' ');
          return `{ const v = ${value.source};
__recordName(${literal(`stringify:${i}`)}, () => JSON.stringify(v));
__recordName(${literal(`indent:${i}`)}, () => JSON.stringify(v, null, ${literal(value.indent)}));
__recordName(${literal(`keys:${i}`)}, () => JSON.stringify(v, ['a', '0', '__proto__']));
__recordName(${literal(`replacer:${i}`)}, () => JSON.stringify(v, (k, x) => typeof x === 'number' ? -x : x));
__recordName(${literal(`roundtrip:${i}`)}, () => JSON.parse(JSON.stringify(v) ?? 'null'));
__recordName(${literal(`reviver:${i}`)}, () => JSON.parse(JSON.stringify(v) ?? '0', (k, x) => typeof x === 'string' ? x.length : x));
__recordName(${literal(`edited:${i}`)}, () => { let t = JSON.stringify(v) ?? ''; ${editsSource} return [t, JSON.parse(t)]; }); }`;
        })
        .join('\n');
      return finish(body);
    }
  };
}

const numericStringParts = [
  '', ' ', '\t', '\n', '\u00a0', '\ufeff', '\u2028', '\u3000', '\u180e', '+', '-', '0', '1', '7', '9', '00', '0x', '0X', '0b', '0o',
  'f', 'F', '.', 'e', 'E', 'e+', 'e-', '5', '308', '400', 'Infinity', 'infinity', 'NaN', '_', '1_0', 'n', 'x', ','
];

function numberCase(random, index) {
  const doubles = Array.from({ length: int(random, 3, 8) }, () => ({
    value: randomDouble(random),
    radix: int(random, 2, 36),
    fixed: int(random, 0, 100),
    precision: int(random, 1, 100),
    exponential: int(random, 0, 100)
  }));
  const strings = Array.from({ length: int(random, 3, 8) }, () => ({
    text: Array.from({ length: int(random, 1, 5) }, () => pick(random, numericStringParts)).join(''),
    radix: pick(random, [undefined, 0, 2, 8, 10, 16, 36, 37])
  }));
  return {
    family: 'number',
    id: `number-${index}`,
    shrinkKeys: ['doubles', 'strings'],
    params: { doubles, strings },
    build(params) {
      const formatted = params.doubles.map(({ value, radix, fixed, precision, exponential }, i) => {
        const x = numberSource(value);
        return `__record(${literal(`string:${i}`)}, () => String(${x}));
${Number.isInteger(value) ? `__record(${literal(`radix:${i}`)}, () => (${x}).toString(${radix}));` : '// toString(radix) of a fraction is implementation-approximated'}
__recordName(${literal(`fixed:${i}`)}, () => (${x}).toFixed(${fixed}));
__recordName(${literal(`precision:${i}`)}, () => (${x}).toPrecision(${precision}));
__recordName(${literal(`exponential:${i}`)}, () => (${x}).toExponential(${exponential}));
__record(${literal(`int32:${i}`)}, () => [(${x}) | 0, (${x}) >>> 0, Math.round(${x}), Math.trunc(${x}), Math.fround(${x})]);`;
      });
      const parsed = params.strings.map(({ text, radix }, i) => {
        const s = literal(text);
        return `__record(${literal(`Number:${i}`)}, () => Number(${s}));
__record(${literal(`parseFloat:${i}`)}, () => parseFloat(${s}));
__record(${literal(`parseInt:${i}`)}, () => parseInt(${s}, ${radix === undefined ? 'undefined' : radix}));
__recordName(${literal(`BigInt:${i}`)}, () => String(BigInt(${s})));`;
      });
      return finish(formatted.concat(parsed).join('\n'));
    }
  };
}

function stringCase(random, index) {
  const pairs = Array.from({ length: int(random, 2, 5) }, () => ({
    s: randomString(random, 8),
    t: randomString(random, 2),
    i: int(random, -2, 10),
    j: int(random, -2, 10),
    n: int(random, 0, 12)
  }));
  return {
    family: 'string',
    id: `string-${index}`,
    shrinkKeys: ['pairs'],
    params: { pairs },
    build(params) {
      const body = params.pairs
        .map(({ s, t, i, j, n }, k) => {
          const S = literal(s), T = literal(t);
          return `{ const s = ${S}, t = ${T};
__record(${literal(`units:${k}`)}, () => [s.length, Array.from({ length: s.length + 1 }, (_, x) => s.charCodeAt(x)), Array.from({ length: s.length + 1 }, (_, x) => s.codePointAt(x))]);
__record(${literal(`chars:${k}`)}, () => [[...s], s.at(${i}), s.charAt(${i}), s[${i}]]);
__record(${literal(`slices:${k}`)}, () => [s.slice(${i}, ${j}), s.substring(${i}, ${j}), s.substr(${i}, ${j})]);
__record(${literal(`search:${k}`)}, () => [s.indexOf(t), s.indexOf(t, ${i}), s.lastIndexOf(t), s.lastIndexOf(t, ${j}), s.includes(t), s.startsWith(t, ${i}), s.endsWith(t, ${j})]);
__record(${literal(`split:${k}`)}, () => [s.split(t), s.split(''), s.split(t, 2)]);
__record(${literal(`replace:${k}`)}, () => [s.replace(t, '[$&]'), s.replaceAll(t, '<$$>'), s.replaceAll('', '.')]);
__record(${literal(`pad:${k}`)}, () => [s.padStart(${n}, t), s.padEnd(${n}, t), s.repeat(2), s.concat(t, s)]);
__record(${literal(`trim:${k}`)}, () => [s.trim(), s.trimStart(), s.trimEnd()]);
__record(${literal(`case:${k}`)}, () => [s.toUpperCase(), s.toLowerCase()]);
__record(${literal(`normalize:${k}`)}, () => ['NFC', 'NFD', 'NFKC', 'NFKD'].map(f => s.normalize(f)));
__record(${literal(`wellformed:${k}`)}, () => [s.isWellFormed(), s.toWellFormed()]);
__recordName(${literal(`uri:${k}`)}, () => [encodeURIComponent(s), encodeURI(s)]);
__recordName(${literal(`uri-roundtrip:${k}`)}, () => decodeURIComponent(encodeURIComponent(s)) === s);
__record(${literal(`escape:${k}`)}, () => [escape(s), unescape(escape(s)) === s]);
__record(${literal(`compare:${k}`)}, () => [s < t, s === t, s > t, [s, t, s + t].sort()]);
__record(${literal(`json:${k}`)}, () => [JSON.stringify(s), JSON.parse(JSON.stringify(s)) === s]);
__record(${literal(`codes:${k}`)}, () => [String.fromCharCode(...Array.from(s, c => c.charCodeAt(0))) === s, Array.from(s, c => c.codePointAt(0))]); }`;
        })
        .join('\n');
      return finish(body);
    }
  };
}

// Source text for a value structuredClone can (or cannot) copy.
function cloneSource(random, depth) {
  const roll = random();
  if (depth <= 0 || roll < 0.35) {
    return pick(random, [
      numberSource(randomDouble(random)), literal(randomString(random, 4)), 'true', 'null', 'undefined', '12345678901234567890n',
      'new Date(1e12)', 'new Date(NaN)', '/a+b/giu', 'new Number(-0)', 'new String("s")', 'new Boolean(false)', 'Object(1n)',
      'new Uint8Array([1, 2, 255])', 'new Int16Array([-1, 300])', 'new Float64Array([NaN, -0, 1.5])', 'new BigInt64Array([-1n])',
      'new DataView(new ArrayBuffer(4), 1, 2)', 'new ArrayBuffer(3)', 'new TypeError("t")', 'new RangeError("r", { cause: 1 })',
      'new Error("e")', '() => 1', 'Symbol("x")', '[, 1, , 2]', 'Object.create(null)'
    ]);
  }
  const count = int(random, 0, 3);
  const items = Array.from({ length: count }, () => cloneSource(random, depth - 1));
  if (roll < 0.55) return `[${items.join(', ')}]`;
  if (roll < 0.7) return `new Map([${items.map((item, i) => `[${i % 2 ? literal(`k${i}`) : item}, ${item}]`).join(', ')}])`;
  if (roll < 0.8) return `new Set([${items.join(', ')}])`;
  return `{ ${items.map((item, i) => `${literal(pick(random, keyPool) + i)}: ${item}`).join(', ')} }`;
}

const describeSource = String.raw`
function __describe(value, ids = new Map()) {
  if (typeof value !== 'object' && typeof value !== 'function') return __canon(value);
  if (value === null) return null;
  if (ids.has(value)) return { $ref: ids.get(value) };
  const id = ids.size;
  ids.set(value, id);
  const tag = Object.prototype.toString.call(value);
  const out = { $id: id, tag, proto: Object.getPrototypeOf(value)?.constructor?.name ?? null };
  if (value instanceof Map) out.entries = [...value].map(([k, v]) => [__describe(k, ids), __describe(v, ids)]);
  else if (value instanceof Set) out.values = [...value].map(v => __describe(v, ids));
  else if (value instanceof Date) out.time = __canon(value.getTime());
  else if (value instanceof RegExp) out.regexp = [value.source, value.flags, value.lastIndex];
  else if (value instanceof Error) out.error = [value.name, value.message, 'cause' in value ? __describe(value.cause, ids) : '$none'];
  else if (ArrayBuffer.isView(value)) out.view = [value.byteOffset, value.byteLength, Array.from(new Uint8Array(value.buffer, value.byteOffset, value.byteLength))];
  else if (value instanceof ArrayBuffer) out.bytes = Array.from(new Uint8Array(value));
  else if (value instanceof Number || value instanceof String || value instanceof Boolean || value instanceof BigInt)
    out.primitive = __canon(value.valueOf());
  if (Array.isArray(value)) out.length = value.length;
  // stack text is engine-specific
  out.own = Reflect.ownKeys(value).filter(k => typeof k === 'string' && k !== 'stack' && !(ArrayBuffer.isView(value) && /^\d+$/.test(k)))
    .map(k => [k, __describe(value[k], ids)]);
  return out;
}`;

function structuredCloneCase(random, index) {
  const values = Array.from({ length: int(random, 2, 5) }, () => ({
    source: cloneSource(random, 3),
    cycle: random() < 0.3,
    shared: random() < 0.3
  }));
  return {
    family: 'structured-clone',
    id: `structured-clone-${index}`,
    shrinkKeys: ['values'],
    params: { values },
    build(params) {
      const body = params.values
        .map(({ source, cycle, shared }, i) => `{ const v = { root: ${source} };
${cycle ? 'v.self = v; if (v.root && typeof v.root === "object") v.root.back = v;' : ''}
${shared ? 'v.again = v.root;' : ''}
__recordName(${literal(`clone:${i}`)}, () => __describe(structuredClone(v)));
__recordName(${literal(`transfer:${i}`)}, () => { const b = new ArrayBuffer(2); const c = structuredClone({ v, b }, { transfer: [b] }); return [b.byteLength, c.b.byteLength, __describe(c.v)]; }); }`)
        .join('\n');
      return finish(`${describeSource}\n${body}`);
    }
  };
}

const byteSegments = [
  [0x61], [0x7f], [0xc3, 0xa9], [0xe2, 0x82, 0xac], [0xf0, 0x9f, 0x98, 0x80], [0xef, 0xbb, 0xbf], [0x80], [0xbf], [0xc0, 0x80],
  [0xc2], [0xe2, 0x82], [0xf0, 0x9f, 0x98], [0xed, 0xa0, 0x80], [0xed, 0xbf, 0xbf], [0xf4, 0x90, 0x80, 0x80], [0xf8], [0xff], [0x00]
];

function textcodecCase(random, index) {
  const inputs = Array.from({ length: int(random, 2, 5) }, () => {
    const bytes = [];
    for (let k = int(random, 0, 8); k > 0; k--) bytes.push(...pick(random, byteSegments));
    const splits = sample(random, [0, 1, 2, 3, 4, 5, 7, 9, 12], 3).sort((a, b) => a - b);
    return { bytes, splits, text: randomString(random, 6), room: int(random, 0, 8) };
  });
  return {
    family: 'textcodec',
    id: `textcodec-${index}`,
    shrinkKeys: ['inputs'],
    params: { inputs },
    build(params) {
      const body = params.inputs
        .map(({ bytes, splits, text, room }, i) => `{ const u8 = new Uint8Array(${literal(bytes)});
__record(${literal(`decode:${i}`)}, () => new TextDecoder().decode(u8));
__record(${literal(`ignoreBOM:${i}`)}, () => new TextDecoder('utf-8', { ignoreBOM: true }).decode(u8));
__recordName(${literal(`fatal:${i}`)}, () => new TextDecoder('utf-8', { fatal: true }).decode(u8));
__record(${literal(`stream:${i}`)}, () => { const d = new TextDecoder(); const cuts = ${literal(splits)}.filter(c => c <= u8.length); let out = '', at = 0; for (const c of cuts) { out += d.decode(u8.subarray(at, c), { stream: true }); at = c; } return out + d.decode(u8.subarray(at)); });
__record(${literal(`utf16le:${i}`)}, () => new TextDecoder('utf-16le').decode(u8));
__record(${literal(`encode:${i}`)}, () => Array.from(new TextEncoder().encode(${literal(text)})));
__record(${literal(`encodeInto:${i}`)}, () => { const dst = new Uint8Array(${room}); const r = new TextEncoder().encodeInto(${literal(text)}, dst); return [r.read, r.written, Array.from(dst)]; });
__record(${literal(`buffer:${i}`)}, () => [Buffer.from(u8).toString('utf8'), Buffer.from(${literal(text)}).length]); }`)
        .join('\n');
      return finish(body);
    }
  };
}

const urlParts = {
  scheme: ['http:', 'https:', 'file:', 'ws:', 'wss:', 'blob:', 'data:', 'mailto:', 'javascript:', 'foo:', 'HTTP:', 'a+b.c-d:', ''],
  slashes: ['//', '/', '', '\\\\', '///', '/\\'],
  user: ['', 'user@', 'u:p@', ':@', '%40@', 'ä@'],
  host: [
    'example.com', 'EXAMPLE.com.', 'xn--nxasmq6b.com', 'ex%41mple.com', '127.0.0.1', '0x7f.1', '2130706433', '1.2.3.4.5', '[::1]',
    '[::ffff:1.2.3.4]', '[1:2::]', '[0:0::0]', 'bücher.de', 'a..b', '', 'local host', '%zz', '\u00ad.com', 'xn--', '256.0.0.1'
  ],
  port: ['', ':80', ':443', ':0', ':8080', ':65535', ':65536', ':abc', ':', ':021'],
  path: ['', '/', '/a/./b/../c', '/%2e%2E/x', '/a b', '/ä', '\\a\\b', '/;x', '//', '/..', '/./.', '/a%2fb', '/C:/x', '/|'],
  query: ['', '?', '?a=1&b=2', '?q=ä ö', '?%zz', "?'\"", '?a=1&a=2&=&x'],
  hash: ['', '#', '#frag', '#ä x', '#%00']
};

function urlCase(random, index) {
  const urls = Array.from({ length: int(random, 3, 8) }, () => ({
    input: Object.values(urlParts).map(values => pick(random, values)).join(''),
    base: pick(random, [null, 'http://base.example/dir/file?x#y', 'file:///C:/x/', 'about:blank', 'https://u:p@h:9/a/b'])
  }));
  const queries = Array.from({ length: int(random, 1, 3) }, () => ({
    query: randomString(random, 6) + pick(random, ['', '&', '=', '+', '%20', '%', '?x=1']),
    name: randomString(random, 3),
    value: randomString(random, 3)
  }));
  return {
    family: 'url',
    id: `url-${index}`,
    shrinkKeys: ['urls', 'queries'],
    params: { urls, queries },
    build(params) {
      const parsed = params.urls.map(({ input, base }, i) => {
        const args = base === null ? literal(input) : `${literal(input)}, ${literal(base)}`;
        return `__recordName(${literal(`url:${i}`)}, () => { const u = new URL(${args}); return [u.href, u.origin, u.protocol, u.username, u.password, u.host, u.hostname, u.port, u.pathname, u.search, u.hash]; });
__record(${literal(`canParse:${i}`)}, () => URL.canParse(${args}));`;
      });
      const searches = params.queries.map(({ query, name, value }, i) => `__record(${literal(`params:${i}`)}, () => {
  const p = new URLSearchParams(${literal(query)});
  const entries = [...p];
  p.append(${literal(name)}, ${literal(value)});
  const appended = p.toString();
  p.sort();
  return [entries, appended, p.toString(), p.get(${literal(name)}), p.has(${literal(name)})];
});`);
      return finish(parsed.concat(searches).join('\n'));
    }
  };
}

const base64Alphabet = 'ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/-_= \n.%'.split('');

function base64Case(random, index) {
  const inputs = Array.from({ length: int(random, 2, 5) }, () => ({
    bytes: Array.from({ length: int(random, 0, 10) }, () => int(random, 0, 255)),
    text: Array.from({ length: int(random, 0, 12) }, () => pick(random, base64Alphabet)).join(''),
    latin1: Array.from({ length: int(random, 0, 6) }, () => String.fromCharCode(int(random, 0, 300))).join('')
  }));
  return {
    family: 'buffer-base64',
    id: `buffer-base64-${index}`,
    shrinkKeys: ['inputs'],
    params: { inputs },
    build(params) {
      const body = params.inputs
        .map(({ bytes, text, latin1 }, i) => `{ const b = Buffer.from(${literal(bytes)});
__record(${literal(`encode:${i}`)}, () => ['base64', 'base64url', 'hex', 'latin1', 'binary', 'ascii', 'ucs2'].map(e => b.toString(e)));
__record(${literal(`decode:${i}`)}, () => ['base64', 'base64url', 'hex', 'latin1', 'utf8', 'ucs2'].map(e => Array.from(Buffer.from(${literal(text)}, e))));
__record(${literal(`byteLength:${i}`)}, () => ['base64', 'base64url', 'hex', 'utf8', 'latin1'].map(e => Buffer.byteLength(${literal(text)}, e)));
__record(${literal(`roundtrip:${i}`)}, () => [Array.from(Buffer.from(b.toString('base64'), 'base64')), Array.from(Buffer.from(b.toString('base64url'), 'base64url'))]);
__recordName(${literal(`btoa:${i}`)}, () => btoa(${literal(latin1)}));
__recordName(${literal(`atob:${i}`)}, () => atob(${literal(text)}));
__recordName(${literal(`atob-btoa:${i}`)}, () => atob(btoa(String.fromCharCode(...b))) === String.fromCharCode(...b)); }`)
        .join('\n');
      return finish(body);
    }
  };
}

export const oracleBuilders = {
  json: jsonCase,
  number: numberCase,
  string: stringCase,
  'structured-clone': structuredCloneCase,
  textcodec: textcodecCase,
  url: urlCase,
  'buffer-base64': base64Case
};
