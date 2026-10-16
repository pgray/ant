const assert = require('node:assert');

const canon = t => { try { return Intl.getCanonicalLocales(t).join(','); } catch (e) { return e.name; } };
const cases = {
  'de-1996-1996': 'RangeError', 'de-1996-1901': 'de-1901-1996', 'en-u-ca-gregory-u-nu-thai': 'RangeError',
  root: 'RangeError', abcd: 'RangeError', 'en-a123': 'RangeError', 'x-private': 'RangeError', 'zh-cmn-hans': 'RangeError',
  'en-GB-oed': 'RangeError', 'en-': 'RangeError', 'en-x': 'RangeError', 'en-u-foo-bar-ca-x': 'RangeError',
  'en-u-nu-thai-ca-gregory': 'en-u-ca-gregory-nu-thai', 'en-u-ca-true': 'en-u-ca', 'en-u-kn-true': 'en-u-kn',
  'en-u-ca-gregory-ca-buddhist': 'en-u-ca-gregory', 'en-u-bbb-aaa': 'en-u-aaa-bbb', 'en-z-foo-a-bar': 'en-a-bar-z-foo',
  'EN-latn-us': 'en-Latn-US', 'zh-hant-tw': 'zh-Hant-TW', 'en-t-de-h0-hybrid': 'en-t-de-h0-hybrid',
  iw: 'he', in: 'id', cmn: 'zh', ji: 'yi', mo: 'ro', tl: 'fil', aar: 'aa', heb: 'he', sh: 'sr-Latn', 'sh-Cyrl': 'sr-Cyrl',
  cnr: 'sr-ME', 'en-us-posix': 'en-US-u-va-posix', 'en-posix': 'en-u-va-posix', 'de-posix-1996': 'de-1996-posix',
  'art-lojban': 'jbo', 'zh-guoyu': 'zh', 'zh-hakka': 'hak', 'hy-arevmda': 'hyw', 'no-bokmal': 'nb',
  'el-polytoni': 'el-polyton', 'sv-aaland': 'sv-AX', 'ja-latn-hepburn-heploc': 'ja-Latn-alalc97',
};
for (const [tag, expected] of Object.entries(cases)) assert.strictEqual(canon(tag), expected, tag);

assert.throws(() => Intl.getCanonicalLocales({ get length() { throw new RangeError('len'); } }), RangeError);
assert.throws(() => Intl.getCanonicalLocales({ length: 1, get 0() { throw new RangeError('item'); } }), RangeError);
assert.throws(() => Intl.getCanonicalLocales({ length: { valueOf() { throw new SyntaxError('vo'); } } }), SyntaxError);
assert.throws(() => Intl.getCanonicalLocales({ length: 1n }), TypeError);
assert.strictEqual(new Intl.NumberFormat('iw').resolvedOptions().locale, 'he');

console.log('Intl canonicalize OK');
