# Parsed native results in RAM — 070A

**A.04 — Verificado no escopo local.** An on-demand RAM cache retains parsed
successful native results so repeated exact disk-cache hits avoid parsing the
same B-rep/mesh/report JSON again. The full current disk payload still passes
its header, request key and SHA-256 checks on every hit. Producer identity,
explicit inputs, portable restore validation and document revisions are unchanged.

The default retention allowance is **256 MiB**, with at most **128 entries**.
`ARK_NATIVE_MEMORY_CACHE_MAX_MB=0` disables it; values from 1 through 1024 select
the MiB allowance. Invalid values retain the default. Allocation happens only
after a validated disk read; there is no reservation of the entire allowance.
LRU eviction and oversized-entry bypass preserve the ordinary disk/kernel path.
`ARK_NATIVE_RESULT_CACHE=0` bypasses the native cache globally.

`nativeKernelInfo.native_memory_cache` reports the effective capacity, retained
allocation accounting, entries, hits, misses, evictions and oversized skips.
Accounting includes JSON values, vector/string capacities and a conservative
entry allowance. It excludes allocator bookkeeping, transient parsing, copies
returned to callers and isolated OCCT worker processes. It is **not a bound on
total application RAM**. Callers receive independent values; mutating one result
cannot change another caller's cached evidence.

## Local measurements

The synthetic fixture is one native sphere with radius 50 mm, with nine warm
identical requests per strategy in the same Windows Release process. Each full
result is compared byte-for-byte, including geometry, semantic references and
reports. Both strategies read and authenticate the disk cache; only the parsed
RAM tier differs. No cold-kernel or whole-assembly speedup is inferred.

| Measurement | RAM disabled | RAM allowance 64 MiB |
| --- | ---: | ---: |
| Median exact request | 14.6266 ms | 3.8985 ms |
| Observed host private bytes | 12,996,608 | 30,806,016 |
| Observed host working-set bytes | 15,876,096 | 33,378,304 |
| Host peak working-set bytes | 29,487,104 | 44,777,472 |

The warm entry accounts for **10,132,334 retained bytes**. Peak working-set
measurements are cumulative process high-water marks, not isolated incremental
allocation measurements. A 1 MiB pressure fixture retained 1,015,180 accounted
bytes in ten entries after eviction; a larger sphere entry was explicitly skipped.

The focused suite passed **130 checks**. Coverage includes exact result equality,
copy isolation, cap shrink, oversized bypass, LRU eviction and correct reloading,
failed-operation exclusion, missing files, and corruption/replacement with the
same size and timestamp. The existing 031B exact-cache and 032D portable-identity
suites also passed.

Reproducible test: `native-memory-cache-070a`. The local raw sample receipt is
`out/performance-070A/native-ram-cache-070a.json`. Measurements use synthetic
geometry and contain no user drawings. Broader assembly latency and production
memory behavior remain outside this fixture's verification scope.
