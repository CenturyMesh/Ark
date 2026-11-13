# 070A: schedule changed work through its dependencies

**A.04 / B.01 / B.03 — Verificado no escopo local.**

The feature scheduler now maintains the set of changed, audited operations and
releases their dependants when an input becomes available. It no longer searches
every later history row after each publication, or checks every possible future
when only a few jobs are running. Published input lookup uses history indices.

Cached geometry never enters this scheduling frontier. Successful cached input
generations remain available to independent jobs immediately, as before.
Completed or published results release their waiting consumers once. Failed,
empty, suppressed and unloaded inputs do not release speculative descendants.
Named-resource and other owner-only operations keep their existing authority
boundary. Publication still follows document history order.

A single ready dependency chain evaluates directly on the owner instead of
passing each sequential operation through a worker. Independent branches retain
bounded parallel evaluation. Pure mesh workers copy their actual inputs and
expression environment; their evaluator does not access `Document`, so it no
longer copies a potentially long ancestor recipe chain.

No native tolerance, model acceptance rule, revision requirement, geometric
authority, document semantic version or parallelism default changed.

## Same-workload measurements

069D and 070A ran the same synthetic commands on the same machine with
`ARK_CPU_THREADS=4`, `ARK_FEATURE_THREADS=4`, and `ARK_NATIVE_THREADS=1`. Each case
used two warmup edits and five measured edits. Values are medians from the MCP
process, with no competing compilation during the measured runs. Complete apply
includes the surrounding transaction work; recompute measures the model engine.

| Synthetic workload | Recompute 069D → 070A | Complete apply 069D → 070A |
|---|---:|---:|
| 768-feature mesh chain, all geometry cached | 54.56 → 4.70 ms | 92.68 → 44.53 ms |
| Same chain, one changed leaf | 54.60 → 4.66 ms | 85.07 → 35.58 ms |
| 768 independent analytic boxes, all cached | 11.07 → 5.28 ms | 51.48 → 47.33 ms |
| Same boxes, one changed feature | 11.04 → 5.30 ms | 44.09 → 40.53 ms |

With a single worker, the 070A chain took 4.72 ms for cached refresh and 4.67 ms
for a leaf edit. Enabling four workers therefore no longer adds the previous
approximately 50 ms scheduling cost when little geometry needs evaluation.
This removes repeated scheduling work; it does not claim that a serial
dependency chain can execute its dependent operations simultaneously.

The regression test verifies zero readiness checks, input lookups and future
harvest checks for the entirely cached 768-feature chain. A leaf edit creates
one dirty candidate and performs one readiness check, with at most eight indexed
input lookups and zero copied ancestor recipes. Unchanged bodies retain their
original immutable cache objects. The older nested traversal inspected every
later candidate at each history row even when those candidates were cached.

For a separate **070A-only**, same-binary comparison of six independent native
box → transform → hole branches, one worker took 33.30 ms recompute and four
workers took 27.34 ms (35.50 → 29.60 ms complete apply). All 18 features evaluated
in both cases; observed peak feature concurrency increased from one to four,
with one native thread allowed per worker. All 30 terminal exact-property
comparisons matched. These are warm local native-cache measurements, not a
qualification of uncached OCCT construction or universal four-core scaling.

The paired 069D/070A workloads retained identical final geometry summaries. The
native regression additionally checks complete mesh buffers, native reports,
semantic topology, history order, source validity and failure equivalence across
serial and parallel execution. It passed **7,812 checks**, including stale
revision rejection, atomic rollback, suppression, forward references, history
rollback, unchanged-cache custody and a two-CPU configuration with a larger
requested worker count.

Two fixture corrections are retained in the evidence: the initial MCP mesh used
nested positions and was rejected before measurement; it was corrected to the
documented flat positions. An initial native test parameter named `width`
conservatively matched unrelated literal `width` property names in the existing
cache-key scanner; naming the intended branch parameter `BranchWidth` isolated
that test. Production behavior was unchanged by both fixture corrections. All
final measured workloads completed without failed operations.

Reproducible scripts and receipts are under `out/performance-070a/`:
`scheduler_benchmark.py`, `native_branch_benchmark.py`, their serial/parallel and
baseline/candidate reports, and `scheduler-comparison.json`.

## CPU and memory controls

Set these environment variables before starting the process:

| Control | Default and accepted scope |
|---|---|
| `ARK_CPU_THREADS` | Defaults to hardware threads capped at 32; accepts 1..64 and clamps to hardware availability. |
| `ARK_FEATURE_THREADS` | Defaults to `min(CPU allowance, 4)`; accepts 1..16. The existing minimum of four audited operations for feature parallelism remains. |
| `ARK_NATIVE_THREADS` | Defaults to 8; accepts 1..64. Parallel native allowance is capped by `CPU allowance / feature workers`. |

The tests establish limits **per document recompute**. There is no new global
native-thread reservation across independently concurrent sessions. The shared
host pool and resident native workers retain their existing lifetimes.

The scheduler's outstanding snapshot admission allowance remains **128 MiB**,
with no environment override. It accounts conservatively for retained input
context and completed outputs; it is not a limit on total application RAM or
isolated OCCT process memory. Completed outputs retain their slot and charge
until publication. Permanently oversized or unsupported worker inputs use the
owner path. Temporary memory pressure keeps a ready job pending, so very large
snapshots may still incur retries. This change neither reserves extra RAM in
advance nor raises that allowance without a measured workload.

`ARK_NATIVE_CACHE_MAX_MB` controls a separate disk cache, not the scheduler's RAM
allowance. The decoded native-result RAM cache is a separate 070A mechanism.
No GPU offload or machine-specific assembly is introduced by this scheduler
change. Broader hardware acceleration evidence belongs to the 070A delivery
record.
