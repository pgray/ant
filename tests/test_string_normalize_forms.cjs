const assert = require('node:assert');

const cases = [
  ['é', 'é', 'é', 'é', 'é'],
  ['é', 'é', 'é', 'é', 'é'],
  ['ẛ̣', 'ẛ̣', 'ẛ̣', 'ṩ', 'ṩ'],
  ['ﬁ', 'ﬁ', 'ﬁ', 'fi', 'fi'],
  ['Å', 'Å', 'Å', 'Å', 'Å'],
  ['가', '가', '가', '가', '가'],
  ['각', '각', '각', '각', '각'],
  ['가ᆧ', '가ᆧ', '가ᆧ', '가ᆧ', '가ᆧ'],
  ['ạ́̀', 'ạ́̀', 'ạ́̀', 'ạ́̀', 'ạ́̀'],
  ['ạ́', 'ạ́', 'ạ́', 'ạ́', 'ạ́'],
  ['ﷺ', 'ﷺ', 'ﷺ', 'صلى الله عليه وسلم', 'صلى الله عليه وسلم'],
  ['𝅗𝅥', '𝅗𝅥', '𝅗𝅥', '𝅗𝅥', '𝅗𝅥'],
  ['x\ud800é\udc00', 'x\ud800é\udc00', 'x\ud800é\udc00', 'x\ud800é\udc00', 'x\ud800é\udc00'],
  ['plain ascii', 'plain ascii', 'plain ascii', 'plain ascii', 'plain ascii'],
  ['', '', '', '', ''],
];

for (const [input, nfc, nfd, nfkc, nfkd] of cases) {
  const label = JSON.stringify(input);
  assert.strictEqual(input.normalize(), nfc, `default ${label}`);
  assert.strictEqual(input.normalize('NFC'), nfc, `NFC ${label}`);
  assert.strictEqual(input.normalize('NFD'), nfd, `NFD ${label}`);
  assert.strictEqual(input.normalize('NFKC'), nfkc, `NFKC ${label}`);
  assert.strictEqual(input.normalize('NFKD'), nfkd, `NFKD ${label}`);
}

assert.throws(() => 'a'.normalize('nfc'), RangeError);

const text = 'Crème brûlée, déjà vu — naïve café ';
for (const form of ['NFC', 'NFD', 'NFKC', 'NFKD']) {
  const once = text.normalize(form);
  assert.strictEqual(once.normalize(form), once, `${form} is idempotent`);
}

assert.strictEqual('ǅ'.toUpperCase(), 'Ǆ');
assert.strictEqual('ǅ'.toLowerCase(), 'ǆ');
assert.strictEqual('ΣΑΣ'.toLowerCase(), 'σας');
assert.strictEqual('ß'.toUpperCase(), 'SS');
assert.strictEqual('𐐨'.toUpperCase(), '𐐀');

console.log('String.prototype.normalize forms ok');
