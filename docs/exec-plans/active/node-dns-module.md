# Node dns Module

Status: active
Last reviewed: 2026-10-15
Owner: theMackabu

## Goal

Ship Node's `dns` and `dns/promises` for 16.1 (issue #113: `@redis/client`
requires `dns/promises`), alongside `assert/strict`. Every DNS operation runs
on the isolate's event loop; nothing blocks the JS thread.

## Starting point

`src/modules/dns.c` exposed only `dns.promises.lookup` and
`dns.promises.resolve`. `lookup` called `getaddrinfo` synchronously and
ignored its options; `resolve` drove c-ares through a blocking `select` loop
and rejected with plain errors. There was no callback API, no `dns/promises`
specifier, no `Resolver`, and no error constants.

## Scope

- `lookup(hostname[, options], cb)` and `lookupService(address, port, cb)`
  through `uv_getaddrinfo` / `uv_getnameinfo`. Options: `family` (0, 4, 6,
  'IPv4', 'IPv6'), `all`, `order` ('verbatim', 'ipv4first', 'ipv6first'),
  `verbatim`, `hints` (`ADDRCONFIG`, `V4MAPPED`, `ALL`). IP literals resolve
  without a system call, as in Node.
- `resolve` and `resolve4/6` (with `{ ttl }`), `resolveAny`, `resolveCaa`,
  `resolveCname`, `resolveMx`, `resolveNaptr`, `resolveNs`, `resolvePtr`,
  `resolveSoa`, `resolveSrv`, `resolveTlsa`, `resolveTxt`, `reverse`.
- `getServers`, `setServers`, `setDefaultResultOrder`,
  `getDefaultResultOrder`, error code constants, `ADDRCONFIG`, `V4MAPPED`,
  `ALL`.
- `Resolver` (callback) and `promises.Resolver` with `cancel()`,
  `setLocalAddress()`, `getServers()`/`setServers()` and constructor options
  `timeout` and `tries`.
- `dns/promises` is the same object as `dns.promises`.

## Design

- **Static data.** Record types (name, c-ares type, method, Node `syscall`)
  live in the X-macro list `include/dns/record_types.h`; c-ares status to
  Node code mapping lives in `include/dns/error_codes.h`.
- **c-ares on libuv.** Each channel registers `ARES_OPT_SOCK_STATE_CB`; the
  callback starts or stops a `uv_poll_t` per socket and calls
  `ares_process_fd`. A `uv_timer_t` re-armed from `ares_timeout()` handles
  retries and timeouts. No c-ares event thread: callbacks must run on the
  isolate's thread.
- **Records** are parsed with the `ares_dns_record_t` API
  (`ares_query_dnsrec`), not the deprecated `ares_parse_*_reply` functions.
  One answer-section walk serves every type, including TTLs and `ANY`.
- **Ownership.** Each isolate owns a lazily created default channel and the
  lists `js->dns_channels` and `js->dns_requests` (`list.h`). A request pins
  its promise or callback, its hostname and its owning `Resolver`, and
  `gc_mark_dns` marks them. A `Resolver` object owns its channel; its
  finalizer destroys the channel, and pending queries keep the object alive.
- **Delivery.** Promise and callback forms share one completion path. Callback
  delivery follows `timer_callback`: `GC_UV_CALLBACK()`, pin, `sv_vm_call`,
  report uncaught exceptions, drain microtasks.
- **Teardown.** `cleanup_dns_module` runs in `js_destroy` before
  `Ant_Loop_Drain`: it marks the isolate as tearing down so completions skip
  JS, destroys channels (c-ares answers `ARES_EDESTRUCTION`), cancels
  `uv_getaddrinfo` requests and closes poll and timer handles.

## Decision log

- Errors match Node: `getaddrinfo ENOTFOUND <host>` with `errno`, `code`,
  `syscall`, `hostname`; resolver errors use `query<Type>` / `getHostByAddr`
  syscalls; `EAI_NONAME`/`EAI_NODATA` map to `ENOTFOUND` like Node.
- Default result order is `verbatim` (Node 17+).
- Ant has no `dgram`, so resolver tests start a small Python UDP DNS server
  and skip when `python3` is missing. `lookup` tests use `localhost` and IP
  literals only; no test touches the network.

## Validation status

- `assert/strict` is done (`tests/test_assert_strict.cjs`).
- dns is implemented in `src/modules/dns/` (`request.c`, `lookup.c`,
  `channel.c`, `query.c`, `module.c`). `tests/test_dns_lookup.cjs` passes on
  Ant and Node. `tests/test_dns_resolver.cjs` passes on Ant; Node fails only its
  `resolveAny` check, because Node returns null entries and drops `type` from
  TLSA records there. Ant groups `resolveAny` records in Node's type order.
- Probes: 600 concurrent queries, 3000 short-lived `Resolver`s under
  allocation churn (open fds 11 before, 13 after), and `process.exit()` with
  queries pending all finish cleanly.
- `test_io_error_boundary_audit.cjs` and `test_io_handled_errors.cjs` asserted
  the old non-Node behavior (rejections for `lookup()`, `lookup(42)` and an
  invalid `rrtype`); they now assert Node's results.

## Follow-ups

- `dgram` would let the DNS fixture be written in JS.
