# CPU, RAM and GPU residency — 070A

**A.02/A.04/B.01/B.02/D.02/F.05 — Verificado no escopo local**, with evidence in
[070A](iterations/070A.md). This iteration addresses repeated scheduling,
parsing, packing and transfers while preserving authoritative native geometry.

## Work placement

Exact B-rep construction stays in the audited OCCT producers. Independent
features can run in parallel; publication remains in document order with the
same revision and failure rules. OCCT also exposes internal parallel algorithms,
including [Boolean operation parallelism](https://occt3d.com/dev/doc/overview/html/specification__boolean_operations.html).
This does not make dependent history steps independent.

Packed display positions use f64 matrix arithmetic with f32 output. On MSVC
x64, a separate AVX2 translation unit handles four vertices together after
CPUID, OSXSAVE and OS YMM-state checks. Multiplication/addition order is retained
and FMA contraction is disabled. Other machines use the portable reference.
This is compiler-managed SIMD machine code; historical handwritten assembly is
not activated. Ordinary CPU transforms execute outside the CUDA resource mutex;
the first automatic CPU/GPU calibration remains serialized.

The existing CUDA path measures the whole transfer/compute/synchronization
route and requires exact output parity and at least 5% measured benefit. SIMD
improves its CPU comparison too, so a faster CPU result can correctly change
the selected backend. Large batches alone do not establish a GPU win. NVIDIA's
[CUDA best-practices guide](https://docs.nvidia.com/cuda/cuda-c-best-practices-guide/index.html)
explains why transfer cost and device residency must be included.

## Scheduler

The ready queue contains dirty audited candidates. Dependency completion wakes
their actual consumers; harvesting visits only running jobs. Published inputs
are indexed, and a single dependent chain avoids needless worker handoffs.
Audited pure mesh workers do not copy unrelated ancestor recipes. Unsupported
or oversized work retains the owner-thread path.

The existing worker limits and128MiB outstanding snapshot admission allowance
remain. Limits apply per recompute; they are not a global reservation across
multiple Studio processes or independently concurrent sessions. A hardcoded
snapshot allowance is not a promise about total process working set.

## RAM

Validated native disk results gain an on-demand parsed-result LRU, default
**256MiB** and at most 128entries. Every hit still authenticates the complete
current file with its key/header and SHA-256. Removed, replaced or corrupted
files cannot be hidden by RAM. Callers get independent working values; failures
do not enter the cache. Optional retention failure preserves a valid disk read.

Diagnostics report hits/misses/evictions and retained JSON/string/vector
capacities. Allocator overhead, transient parsing, native worker memory and
caller-owned copies are separate from this retained-byte allowance.

## VRAM

Mesh source positions, indices and normals are fingerprinted before packing.
An exact cache hit skips normalization, packing and upload. Changing original
normals invalidates the key even if SNORM16 rounding would produce equal display
normals. CPU geometry, exact references and picking authority are unchanged.

Idle display buffers adapt to the graphics adapter's reported capacity and free
memory, normally up to **256MiB**. Unknown adapters retain the prior 32MiB
fallback. Low free memory shrinks idle residency, and active shared meshes are
never evicted. Storage is acquired for actual meshes, not preallocated to fill
the allowance. Optional
[NVX memory telemetry](https://registry.khronos.org/OpenGL/extensions/NVX/NVX_gpu_memory_info.txt)
is approximate and cannot guarantee a future allocation; it is sampled at most
once every two seconds. This policy concerns retained display buffers, not
GPU-native B-rep construction or the application's entire VRAM footprint.

## Existing and new controls

| Process environment setting | Scope |
|---|---|
| `ARK_CPU_THREADS` | Per-recompute logical CPU allowance 1..64, clamped to hardware |
| `ARK_FEATURE_THREADS` | Feature workers 1..16; default up to 4 |
| `ARK_NATIVE_THREADS` | Requested native threads 1..64; default 8, divided within the recompute allowance |
| `ARK_NATIVE_MEMORY_CACHE_MAX_MB` | New parsed RAM allowance 0..1024MiB; default 256 |
| `ARK_GPU_MESH_IDLE_MB` | New requested idle display allowance 0..1024MiB, further constrained by adapter/pressure policy |
| `ARK_DISABLE_SIMD=1` | Portable CPU reference for an explicit comparison |

These are process-local performance controls, not agent turn/token budgets.
`ARK_NATIVE_CACHE_MAX_MB` remains a **disk** cache control and does not change
the scheduler's RAM admission allowance. `compute_info` reports the selected CPU
implementation, CUDA availability/profiles and actual retained transform-buffer
bytes. `compute_benchmark` includes transfers; sample inputs are explicit and
bounded. No setting changes modelling tolerances or allows stale publication.

## Closed Studio connections

Windows `ark-mcp --connect` forwarding proxies now observe a persistently absent
Studio endpoint while idle instead of waiting forever for a client that keeps
stdin open. Busy/unknown endpoints and in-flight calls are preserved. Standalone
headless documents and memory hosts retain their previous input lifecycle.
Installer refusals now identify the executable and PID, including background
connectors, rather than describing every process as an open application window.
The installer still blocks active runtimes and does not terminate them.
