for (const i of [1,2]) { try { await import("./bad.mjs"); console.log("import " + i + " resolved") } catch (e) { console.log("import " + i + ": " + e.message) } }
