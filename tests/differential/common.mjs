// Shared generator helpers and the probe prelude.

export function mulberry32(seed) {
  let state = seed >>> 0;
  return () => {
    state = (state + 0x6d2b79f5) | 0;
    let value = Math.imul(state ^ (state >>> 15), 1 | state);
    value ^= value + Math.imul(value ^ (value >>> 7), 61 | value);
    return ((value ^ (value >>> 14)) >>> 0) / 4294967296;
  };
}

export function pick(random, values) {
  return values[Math.floor(random() * values.length)];
}

export const literal = JSON.stringify;

const prelude = String.raw`
const __out = [];
function __canon(value, seen = new Set()) {
  if (value === undefined) return { $type: 'undefined' };
  if (typeof value === 'number') {
    if (Number.isNaN(value)) return { $type: 'number', value: 'NaN' };
    if (value === Infinity) return { $type: 'number', value: 'Infinity' };
    if (value === -Infinity) return { $type: 'number', value: '-Infinity' };
    if (Object.is(value, -0)) return { $type: 'number', value: '-0' };
    return value;
  }
  if (typeof value === 'bigint') return { $type: 'bigint', value: String(value) };
  if (typeof value === 'symbol') return { $type: 'symbol', value: String(value) };
  if (typeof value === 'function') return { $type: 'function', name: value.name, length: value.length };
  if (value === null || typeof value !== 'object') return value;
  if (seen.has(value)) return { $type: 'circular' };
  seen.add(value);
  if (value instanceof Error) {
    const result = { $type: 'error', name: value.name, message: value.message };
    seen.delete(value);
    return result;
  }
  const result = Array.isArray(value) ? [] : {};
  for (const key of Object.keys(value).sort()) result[key] = __canon(value[key], seen);
  seen.delete(value);
  return result;
}
function __record(label, callback) {
  try { __out.push([label, __canon(callback())]); }
  catch (error) { __out.push([label, __canon(error)]); }
}
// error messages differ between engines; only the error type is compared
function __recordName(label, callback) {
  try { __out.push([label, __canon(callback())]); }
  catch (error) { __out.push([label, { $type: 'error', name: error && error.name }]); }
}
`;

export function finish(body, asynchronous = false) {
  if (asynchronous) {
    return `${prelude}\n(async () => {\n${body}\nconsole.log(JSON.stringify(__out));\n})().catch(error => { console.log(JSON.stringify([['$fatal', __canon(error)]])); process.exitCode = 1; });\n`;
  }
  return `${prelude}\n${body}\nconsole.log(JSON.stringify(__out));\n`;
}

export function int(random, min, max) {
  return min + Math.floor(random() * (max - min + 1));
}

export function sample(random, values, count) {
  return Array.from({ length: count }, () => pick(random, values));
}

// Source text for a number, including the values String() loses.
export function numberSource(value) {
  if (Number.isNaN(value)) return 'NaN';
  if (value === Infinity) return 'Infinity';
  if (value === -Infinity) return '-Infinity';
  if (Object.is(value, -0)) return '-0';
  return String(value);
}

// Strings mixing ASCII, escapes, Latin-1, BMP, astral pairs and lone surrogates.
const codeUnitPool = [
  'a', 'Z', '0', '9', ' ', '\t', '\n', '\r', '"', "'", '\\', '/', '<', '&', '=', '+', '%', '.', '-', '_',
  '\u0000', '\u001f', '\u007f', ' ', 'é', 'ß', 'İ', 'ﬀ', 'Σ', 'ς', 'ǅ',
  ' ', ' ', '﻿', '€', '中', '\u{1F600}', '\u{10FFFF}', '\uD800', '\uDFFF', '\uD83D', '\uDE00'
];

export function randomString(random, maxLength) {
  const length = int(random, 0, maxLength);
  let out = '';
  for (let i = 0; i < length; i++) out += pick(random, codeUnitPool);
  return out;
}
