# 071D: hardware-aware RAM and worker ceilings

**A.04 / D.02 — Verificado no escopo local.**

ARK selects memory and CPU allowances from detected hardware and current
available physical memory. These are demand-driven ceilings: starting ARK or
raising a ceiling does not allocate that amount of RAM. The decoded-result cache
retains actual reusable results; the scheduler admits actual changed work.

## RAM selection

The default decoded native-result target is one sixteenth of total RAM, capped
at 16 GiB. The default outstanding worker-snapshot target is one sixty-fourth,
capped at 4 GiB. For healthy free-memory conditions:

| Total RAM | Decoded-result target | Snapshot-admission target |
|---:|---:|---:|
| 2 GiB | 128 MiB | 32 MiB |
| 8 GiB | 512 MiB | 128 MiB |
| 16 GiB | 1 GiB | 256 MiB |
| 32 GiB | 2 GiB | 512 MiB |
| 64 GiB | 4 GiB | 1 GiB |

There is no assumption that every machine has at least 16 GiB. Actual visible
memory can be slightly lower than installed memory, so the selected ceilings
can be slightly lower than this table. Targets are rounded down to MiB.

A physical-memory reserve is `min(total/2, 4 GiB, max(256 MiB, total/8))`.
After that reserve:

- Snapshot admission is bounded by one quarter of currently available headroom.
- Decoded cache retention is bounded by half the headroom after adding back its
  own currently retained allocation. This avoids interpreting the cache's own
  successful growth as unrelated memory pressure.
- The worker ceiling is also constrained to one worker per 256 MiB of available
  headroom, with an owner-only path when no parallel allowance remains.

Cache lookup, insertion and inspection sample pressure and trim old entries.
New snapshot admission samples available RAM again; pressure can tighten it
during a recompute. Running geometry is allowed to finish, and worker-count
changes take effect at the next recompute boundary. There is no background
preallocation or forced termination of an in-flight modeling operation.

The decoded entry ceiling scales with its effective capacity: one entry per
2 MiB, bounded to 128..4096 entries. This allows larger working sets to use the
additional RAM instead of retaining the previous fixed 128-entry limit.
Every hit still authenticates the complete current disk payload with SHA-256.
Missing, corrupted or replaced files cannot be recovered solely from a RAM copy.

Unknown memory telemetry uses conservative 256 MiB decoded / 128 MiB snapshot
fallbacks and at most four automatic workers. Unknown physical-core topology
also uses the conservative worker fallback. The Windows implementation reads
physical/logical topology and live available physical memory; the deterministic
low-memory tests do not claim qualification on every physical machine size.

## Worker selection

The automatic ceiling is the minimum of 16, detected physical cores, and half
the logical threads. CPU allowance, memory pressure, actual dirty audited
tasks and the dependency graph's estimated parallel width can lower it. Fewer
than four dirty audited tasks use the owner path.
Thus a cached document or one changed feature does not create a pool of sixteen
active modeling jobs merely because the computer has many cores.

The width estimate is a single pass over the existing dependency graph, counting
dirty operations in dependency layers while treating cached inputs as available.
It is conservative, not an optimal maximum-antichain calculation. Four long
independent chains therefore select at most four feature workers and retain the
larger native allowance for each, rather than dividing CPU allowance by sixteen
workers that the graph cannot use. A later wide fan-out can still select a
larger worker ceiling. Entirely cached rows skip the dirty dependency scan.

For a machine with 24 physical cores and 32 logical threads, the hardware
ceiling is 16 feature workers. The scheduler can hold hundreds of pending graph
operations while running only the independent operations admitted by this
ceiling. Pending work is not one operating-system worker per operation.

Parallel native allowance is bounded by `CPU allowance / selected workers`.
When no other operation is outstanding, owner evaluation can use the full
requested native allowance (capped by CPU allowance). Raising the feature-worker
ceiling therefore does not silently throttle a serial dependency chain from
eight native threads to two. Ordered publication, dependency checks, source
hashes, revision checks and atomic rejection remain unchanged.

The controls are per recompute/process. They are not a global reservation across
multiple independently running ARK sessions or a guarantee on total process RAM.
Decoded accounting excludes allocator overhead and caller-owned working copies;
snapshot accounting excludes isolated OCCT process memory. Available-memory
sampling reduces additional retention/admission but cannot promise that another
application will not allocate memory concurrently.

## Explicit controls and diagnostics

Set environment overrides before launching ARK:

| Override | Meaning |
|---|---|
| `ARK_CPU_THREADS=1..64` | CPU allowance, clamped to detected logical threads; default remains at most 32. |
| `ARK_FEATURE_THREADS=1..64` | Requested feature-worker ceiling, further clamped by CPU, pressure and actual eligible work. |
| `ARK_NATIVE_THREADS=1..64` | Native allowance; default 8, divided conservatively when feature jobs run in parallel. |
| `ARK_NATIVE_MEMORY_CACHE_MAX_MB=0..65536` | Decoded RAM ceiling in MiB; 0 disables retention, omission selects hardware policy. |
| `ARK_FEATURE_PAYLOAD_MAX_MB=0..65536` | Snapshot-admission ceiling in MiB; 0 preserves owner-only evaluation, omission selects hardware policy. |
| `ARK_NATIVE_RESULT_CACHE=0` | Disables both persistent native-result reuse and its decoded RAM tier. |

Explicit memory values are ceilings, so available-memory pressure can lower the
effective allowance. Invalid CPU/native/worker or new snapshot controls reject.
An invalid legacy decoded-cache override retains its fallback behavior and now
appears as an explicit warning in the policy receipt. `ARK_NATIVE_CACHE_MAX_MB`
continues to control the separate disk cache.

`kernel_info.resource_policy` and `kernel_info.native_memory_cache.resource_policy`
report hardware, available memory, reserve, target/effective limits, automatic
and requested workers, native owner/worker allowances, override flags and
selection reasons. This reports the process policy without a particular dirty
document workload. `doc_info` reports the actual worker and native-worker
allowances selected by its last recompute. The thread-local scheduler diagnostic
also retains the selected dirty-task count, full policy, minimum observed
snapshot limit, estimated parallel width and maximum owner native allowance.

No `.ark` document semantic version changes, geometry approximations or new
agent turn/token limits are introduced by this policy.

## Local measurements and scope

The local hardware reported 24 physical cores, 32 logical threads and approximately
63.77 GiB visible RAM, with about 37 GiB available at the start. The actual
automatic decoded ceiling was **4081 MiB** and the snapshot ceiling **1020 MiB**.
The synthetic benchmarks used the same 071D binaries, commands, dimensions and
geometry checks across settings. No user drawing or session was used.

For 32 independent box → transform → through-hole branches (96 changed native
features), the CPU allowance stayed at 32 and the requested native allowance at
8. Native-result caching was disabled so every measured operation constructed
geometry. Each run used two warmup edits followed by five measured edits:

| Requested feature workers | Native threads per worker | Median recompute | Median complete apply | Owned private commit |
|---:|---:|---:|---:|---:|
| 4 | 8 | 626.41 ms | 637.39 ms | 282.4 MiB |
| 8 | 4 | 443.23 ms | 453.60 ms | 283.8 MiB |
| 12 | 2 | 374.28 ms | 385.64 ms | 282.2 MiB |
| 16 | 2 | 286.24 ms | 296.91 ms | 283.9 MiB |
| 24 | 1 | 291.37 ms | 301.21 ms | 224.3 MiB |
| 32 | 1 | 232.02 ms | 242.15 ms | 221.2 MiB |
| Auto, selected 16 | 2 | 304.05 ms | 314.51 ms | 287.8 MiB |

Observed peak feature concurrency matched each selected ceiling. Auto reduced
recompute by about 51% relative to the four-worker run in this wide workload.
Thirty-two workers were fastest here; 24 did not outperform 16 in this run.
These are local observations, not proof of a universally optimal worker count.

With disk results preseeded separately for each native-thread setting and the
decoded RAM tier warm, the same wide fixture measured **142.79 / 131.18 /
128.81 ms** for 4 / 16 / 32 workers. Owned private commit was approximately
**101 / 103 / 105 MiB**. This smaller additional gain beyond 16 supports keeping
the automatic ceiling conservative while making explicit 32-worker operation
available on this machine. Initial warm-seeding receipts are preserved rather
than mixed into the final memory comparison.

Memory samples include the owned MCP host and its native-worker descendants,
including hidden console hosts. They are committed private bytes, not a measured
system-wide physical-RAM peak; summed RSS can double-count shared pages. Idle
native workers remain associated with host-pool threads. A snapshot containing
33 resident OCCT processes does not mean 33 geometric operations were active:
the concurrent-operation counter is reported separately.

The dependency guards also received direct checks:

- A 17-feature native through-hole chain selected one active worker and eight
  native threads with either a requested ceiling of 4 or 16. Recompute was
  717.87 / 710.67 ms; there is no meaningful claimed chain speedup.
- Four deep chains (68 features) selected four workers and eight native threads
  for both requested ceilings. Recompute was 2869.51 / 2869.93 ms. Higher defaults
  did not throttle their native allowance. Existing bounded unpublished-result
  slots and history publication still limit pipeline throughput in this case.

### When additional RAM helps

A separate workload repeatedly edited one exact sphere through 36 radii. It
compared a 256 MiB decoded ceiling with Auto, using two complete warmup cycles
and two measured cycles. CPU/native settings and all 72 measured edits matched.
The fixture fits under both entry-count limits, so byte capacity causes this
difference:

| Decoded ceiling | Retained, accounted | Retained entries | Measured hits / misses | Median recompute | Median complete apply | Owned private commit |
|---|---:|---:|---:|---:|---:|---:|
| 256 MiB | 247.23 MiB | 24 | 0 / 72 | 19.90 ms | 23.07 ms | 227.58 MiB |
| Auto, 4081 MiB ceiling | 365.56 MiB | 36 | 72 / 0 | 6.35 ms | 9.18 ms | 329.02 MiB |

About **101.43 MiB additional owned private commit** eliminated the repeated
JSON decoding in this working set. Recompute fell about **68%** and complete
apply about **60%**. The cache retained only the requested 36 results; it did
not allocate its roughly 4 GiB ceiling. Cache accounting is conservative and
is distinct from process private commit.

All **9657 numeric geometry-property comparisons** across the worker/chain
comparisons matched exactly, as did all **72 sphere volume/bounds pairs**.
Independent analytic volumes were checked in every native fixture. No final
measured operation failed. The resource-policy suite passed **1074 checks**,
and the scheduler suite passed **7812 checks**, covering deterministic low-memory
inputs, pressure, retained-cache accounting, overrides, actual Windows probing,
cache growth/trim, owner allowances, exact geometry, failures and atomicity.

Reproduction scripts and receipts are under `out/performance-071d/`:
`worker_matrix.py`, `owner_chain.py`, `ram_working_set.py`, `compare.py`, and
`resource-comparison.json`. The measurements establish these local synthetic
workloads, not global process scheduling, arbitrary-project acceleration,
manufacturing qualification or production deployment.
