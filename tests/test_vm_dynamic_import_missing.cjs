const assert = require('node:assert/strict');
const vm = require('node:vm');

async function main() {
  const fromNew = vm.runInNewContext('import("node:fs")');
  assert.equal(typeof fromNew.then, 'function');
  await assert.rejects(fromNew, { code: 'ERR_VM_DYNAMIC_IMPORT_CALLBACK_MISSING' });

  const script = new vm.Script('import("node:path")');
  await assert.rejects(script.runInThisContext(), { code: 'ERR_VM_DYNAMIC_IMPORT_CALLBACK_MISSING' });

  const ctx = vm.createContext({});
  const caught = vm.runInContext('import("x").then(() => "resolved", e => e.code)', ctx);
  assert.equal(await caught, 'ERR_VM_DYNAMIC_IMPORT_CALLBACK_MISSING');

  console.log('ok');
}

main();
