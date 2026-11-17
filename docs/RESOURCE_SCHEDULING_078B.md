# Shared native execution and RAM admission — 078B

**A.04 / B.02 / D.02 — Verificado no escopo do teste nativo dedicado**:
56 checks passed after the shutdown-lifetime fixes. The complete coordinated
210-suite native/script regression and6 focused Studio suites also passed.
This addresses resource ownership and scheduling, not
geometric tolerances or manufacturing qualification.

The focused `evaluation_schedule-032d` cancellation regression also passed
(0.18 s in the local run). Four independent native boxes plus a named-sketch
extrusion require both pool and owner execution; all five executing requests
must cancel explicitly with the inherited 120-second allowance. Source/revision
atomicity and a healthy retry are checked. Process creation and callback poll
counts are diagnostic only, because resident reuse and owner fallback are valid.

## Native workers

Windows native requests now take exclusive leases from a process-wide resident
pool. A process can serve subsequent callers on different host threads. Each
lease retains the existing isolated job, request/result files, producer/path
checks, current per-request thread allowance and watchdog. Errors discard the
leased resident. Idle dead/expired residents are removed; admission and return
trim idle processes to the current worker ceiling. Available-memory headroom
below 256 MiB retains no idle processes. Active work is never killed to reclaim
memory. Hosts recycle idle leases after 20 seconds at maintenance/admission,
leaving a margin before the native process's existing 30-second idle exit.
Both Windows launch paths explicitly whitelist only their own stdin and output
handles. A new worker cannot retain another worker's diagnostic log or an
unrelated inheritable host handle, which otherwise delays cleanup after eviction.

A FIFO admission queue reserves the sum of native CPU allowances across
concurrent sessions in the same host process. The current CPU and worker
ceilings bound entry; running jobs retain their admitted allowance. A caller
requesting more threads than its CPU ceiling is clamped before transmission to
the native worker. One-shot Windows and Linux execution use the same admission
gate. Cancellation is checked while queued, outside the admission mutex, and
removes its queue entry explicitly; it does not create a native process first.

The pool's empty shared ownership exists before `main`, so the lazily created
core thread pool drains before native ownership is destroyed. Cleanup and job
termination are outside the resident bookkeeping mutex. Different producers do
not share an active lease. Generic core tasks are FIFO rather than LIFO, so a
later stream of work does not repeatedly jump ahead of earlier dependencies.

The cache-enabled shutdown regression exposed late static-destructor registration
before the first native process could start. Native digests now use the existing
process-lifetime platform SHA helper; producer pin-map, decoded-cache and body-memo
ownership are established before `main`. Eligibility tables are constant arrays, with no
late constructor/destructor registration. File pins, complete per-lookup SHA
verification and explicit producer identity checks are preserved.

`kernel_info.native_admission` reports active and peak requested CPU threads,
active/queued requests, waits and queued cancellations.
`kernel_info.native_worker_pool` reports resident/busy processes, the current
retention ceiling, reuse and eviction counts. These are real pool counters;
they do not infer process ownership by recycled OS parent-process identifiers.

## RAM admission

The feature scheduler charges each copied ancestor recipe snapshot, the captured
feature/environment/materials and conservative native-request or mesh-output
working copies. It no longer charges the entire unrelated document for every
audited task. Immutable `NativePayload` and `MeshSnapshot` inputs remain shared;
copied analytic trims/profiles are counted. Completed unpublished results retain
their existing measured output charge and slot until ordered publication.

New scheduler diagnostics expose largest snapshot charge, peak admitted payload
bytes and referenced shared-input bytes. These are admission estimates and
ownership evidence, not a global process RSS cap. Allocators, the application,
OS/file caches and other ARK processes have separate memory/CPU ownership.

## Regression scope

- FIFO core execution and nested parallel work.
- Aggregate CPU bounds across concurrent callers; fair waiting, queued
  cancellation and a cancellation predicate that throws.
- Cancellation after real native dispatch uses a thread-local execution phase,
  independent of variable admission polling and resident process reuse. Request
  control inheritance, explicit failures, retry and atomic source rejection are
  checked separately from queued cancellation.
- Actual OCCT reuse from short-lived caller threads, concurrent result
  isolation, failed-operation recovery, idle expiry, changed/relocated producers
  and cleanup. Producer custody pins remain intact; private producer fixtures
  are removed by the parent only after the owning child process exits.
- Child processes that first use native services while their core pool drains
  during shutdown, with cache disabled and with a private cache enabled. The
  enabled case covers repeated disk/RAM hits and late `kernel_info` diagnostics.
- Unrelated inheritable file sentinels in resident and held one-shot workers;
  both must be deletable while the child worker remains alive.
- Serial/parallel geometry parity and a 1 MiB payload allowance with unrelated
  suppressed history that must not disable small independent mesh jobs.

No measured speedup or system-wide CPU/VRAM allocation guarantee is claimed by
these implementation changes alone. The native timeout remains an active-worker
watchdog; FIFO queueing itself is cancellable and does not silently relax the
geometry acceptance contract.
