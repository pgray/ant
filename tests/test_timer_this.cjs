const assert = require('node:assert');

const seen = [];
const timeout = setTimeout(function () { seen.push(['timeout', this === timeout]); }, 1);
const interval = setInterval(function () {
  this.count = (this.count || 0) + 1;
  if (this.count === 2) {
    clearInterval(interval);
    seen.push(['interval', this === interval, interval.count]);
  }
}, 1);
const immediate = setImmediate(function () { seen.push(['immediate', this === immediate]); });
setTimeout(() => seen.push(['arrow', this === module.exports]), 1);

setTimeout(() => {
  assert.deepStrictEqual(seen.sort(), [['arrow', true], ['immediate', true], ['interval', true, 2], ['timeout', true]]);
  console.log('timer this OK');
}, 50);
