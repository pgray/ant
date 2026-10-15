// Intl.getCanonicalLocales and supportedLocalesOf canonicalize locale lists,
// constructors canonicalize their locale, and util.isArray exists.
// Output must match what Node prints.
const assert = require('node:assert');
const { spawnSync } = require('node:child_process');

const body = String.raw`
const t=(n,f)=>{ try { console.log(n, JSON.stringify(f())); } catch (e) { console.log(n, 'throws', e.name, e.message); } };
t('undef', ()=>Intl.getCanonicalLocales());
t('str', ()=>Intl.getCanonicalLocales('EN-us'));
t('script', ()=>Intl.getCanonicalLocales('zh-hant-tw'));
t('list', ()=>Intl.getCanonicalLocales(['en-US','EN-us','fr','de-DE']));
t('args', ()=>(function(){ return Intl.getCanonicalLocales(arguments); })('EN-us','fr'));
t('bad', ()=>Intl.getCanonicalLocales('en_US'));
t('badtype', ()=>Intl.getCanonicalLocales([5]));
t('null', ()=>Intl.getCanonicalLocales(null));
t('empty', ()=>Intl.getCanonicalLocales([]));
t('variant', ()=>Intl.getCanonicalLocales('de-DE-1996'));
t('region3', ()=>Intl.getCanonicalLocales('es-419'));
t('sloc', ()=>Intl.NumberFormat.supportedLocalesOf(['en-US','fr']));
t('sloc2', ()=>Intl.DateTimeFormat.supportedLocalesOf('EN-us'));
t('sloc3', ()=>Intl.Collator.supportedLocalesOf());
t('sloc4', ()=>Intl.Segmenter.supportedLocalesOf(['de']));
t('len', ()=>[Intl.getCanonicalLocales.length, Intl.NumberFormat.supportedLocalesOf.length]);
for (const C of [Intl.NumberFormat, Intl.DateTimeFormat, Intl.Collator, Intl.Segmenter]) {
  t(C.name, ()=>[new C('EN-us').resolvedOptions().locale, new C(['ZH-hant-tw']).resolvedOptions().locale]);
  t(C.name + ' bad', ()=>new C('en_US'));
}
t('util.isArray', ()=>[require('util').isArray([]), require('util').isArray({}), require('util').isArray((function(){return arguments})()), require('util').isArray(new Proxy([], {}))]);
`;

const env = { ...process.env, NO_COLOR: '1' };
delete env.FORCE_COLOR;
const child = spawnSync(process.execPath, ['-e', body], { encoding: 'utf8', timeout: 60000, env });
assert.strictEqual(child.status, 0, child.stderr);
assert.strictEqual(child.stdout, "undef []\nstr [\"en-US\"]\nscript [\"zh-Hant-TW\"]\nlist [\"en-US\",\"fr\",\"de-DE\"]\nargs [\"en-US\",\"fr\"]\nbad throws RangeError Invalid language tag: en_US\nbadtype throws TypeError Language ID should be string or object.\nnull throws TypeError Cannot convert undefined or null to object\nempty []\nvariant [\"de-DE-1996\"]\nregion3 [\"es-419\"]\nsloc [\"en-US\",\"fr\"]\nsloc2 [\"en-US\"]\nsloc3 []\nsloc4 [\"de\"]\nlen [1,1]\nNumberFormat [\"en-US\",\"zh-Hant-TW\"]\nNumberFormat bad throws RangeError Invalid language tag: en_US\nDateTimeFormat [\"en-US\",\"zh-Hant-TW\"]\nDateTimeFormat bad throws RangeError Invalid language tag: en_US\nCollator [\"en-US\",\"zh-Hant-TW\"]\nCollator bad throws RangeError Invalid language tag: en_US\nSegmenter [\"en-US\",\"zh-Hant-TW\"]\nSegmenter bad throws RangeError Invalid language tag: en_US\nutil.isArray [true,false,false,true]\n");
console.log('PASS Intl locale lists match Node');
