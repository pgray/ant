import assert from "node:assert/strict";
import fs from "node:fs";
import os from "node:os";
import path from "node:path";
import { narHash } from "../packages/nix/update-vendor-hashes.js";

const root = fs.mkdtempSync(path.join(os.tmpdir(), "ant-vendor-hash-"));
try {
  const nested = path.join(root, "nested");
  fs.mkdirSync(nested);
  fs.writeFileSync(path.join(nested, "source.c"), "original source\n");
  const clean = narHash(root);
  fs.writeFileSync(path.join(root, ".DS_Store"), "Finder metadata");
  fs.writeFileSync(path.join(nested, ".DS_Store"), "nested Finder metadata");
  assert.equal(narHash(root), clean);
  assert.equal(
    narHash(null, new Map([
      ["nested", nested],
      [".DS_Store", path.join(root, ".DS_Store")],
    ])),
    clean,
  );
  fs.writeFileSync(path.join(nested, "source.c"), "changed source\n");
  assert.notEqual(narHash(root), clean);
  fs.writeFileSync(path.join(nested, "source.c"), "original source\n");
  fs.writeFileSync(path.join(nested, ".DS_Store.keep"), "real input");
  assert.notEqual(narHash(root), clean);
  console.log("Nix vendor hash tests passed");
} finally {
  fs.rmSync(root, { recursive: true, force: true });
}
