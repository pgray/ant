# Wire: Defined Properties, Moved Transfers, Shared Blobs

Status: completed
Last reviewed: 2026-10-20
Owner: theMackabu

## Outcome

`src/modules/wire.c` serializes MessagePort, BroadcastChannel and
worker_threads messages (structuredClone has its own in-heap clone).

- Deserialized object properties are defined with `js_mkprop_fast`
  (CreateDataProperty) instead of `js_setprop` ([[Set]]). A `__proto__` key no
  longer replaces the receiver's prototype and setters on `Object.prototype` no
  longer fire. Keys are interned straight from the wire bytes.
- Transferred ArrayBuffers are moved, not copied. The writer records them, and
  only after serialization succeeds `buffer_move_storage` hands the storage to a
  ref-counted holder (`buffer_storage_holder_new`) owned by the wire and removes
  the owner from the sending isolate's buffer registry, so neither isolate
  touches the other's registry; ref counts are atomic. External-storage buffers
  keep the copying path.
- Blob bytes live in a ref-counted `blob_storage_t`; messages share it instead
  of copying twice.
- `markAsUncloneable` sets `SLOT_UNCLONEABLE` and a per-isolate
  `uncloneable_marked` flag instead of populating a WeakSet, so marking one
  object no longer adds a VM call per serialized object.
- Blob detection uses the brand slot instead of a prototype-chain walk.
- Map/Set entries store their key bytes inline (one allocation per entry), and
  a dying Map/Set frees its hash table with `HASH_CLEAR`. `map.set` of 10k
  entries: 11.7M -> 8.7M instructions.
- `structuredClone` of a Set with object members re-keys entries through
  `collections_set_store_cloned_entry`; it previously copied the source keys,
  so `has(clonedMember)` was false.

## Benchmarks (per message over a MessageChannel, Node 22 / before / after)

- 10 props: 1.1 / 1.0 / 0.7 us; 10k objects: 2.30 / 2.80 / 2.02 ms
- 16 MB transferred: 21.6 / 2050 / ~2 us
- Map 10k: 0.85 / 1.46 / ~0.72 ms

## Validation

`tests/test_wire_define_semantics.cjs`, `tests/test_wire_transfer_move.cjs`,
`tests/test_wire_blob_share.cjs` pass on Node 22 and Ant; ASan and TSan clean,
including a 4-worker cross-thread transfer stress.

## Remaining

- Numbers are always 9 bytes; arrays are filled with `js_arr_push`; BigInts go
  through decimal strings; transfer-list duplicate checks are O(n^2).
- Errors keep extra own properties (e.g. `code`) across postMessage; Node drops
  them per the HTML spec.
- `markAsUncloneable` on an array rejects it; Node ignores the mark for arrays.
