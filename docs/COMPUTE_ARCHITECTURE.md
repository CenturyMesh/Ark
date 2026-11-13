# CPU, GPU and responsive evaluation

The current [multicore evaluator](ENGINEERING_AGENT_V2.md#multicore-parametric-evaluation)
executes audited explicit-dependency branches concurrently and publishes in
feature order. CPU/native budgets, serial barriers, queued-input memory limits
and warm/cold measurement conventions are documented there.

The [agent automation extension](AGENT_AUTOMATION.md#computation-and-validation)
adds conservative incremental feature evaluation, a bounded resident Windows
OCCT worker, and automatically differentiated sketch Jacobians with independent
solve blocks. The worker remains isolated and the session remains serialized.
`ARK_NATIVE_ONESHOT=1` is the transport control for comparative measurements.

The parametric feature history remains ordered so references bind to their
declared producers. Exact B-rep construction and validation run on OCCT CPU
geometry. Studio transfers exclusive ownership of a long session operation
to one worker while displaying a cached, stable frame and an elapsed-time
loading overlay. Other tools wait in the queue; edits resume after completion.
Closing is deferred until the worker joins. The overlay does not invent a
percentage or claim that an uninterruptible kernel call has been cancelled.

Studio's main thread and session worker have an explicit 8 MiB stack reserve
(`ARK_STUDIO_STACK_RESERVE` in CMake). This is address space; pages are
committed only when used. The linker default is 1 MiB with MSVC, and the
local llvm-mingw executable header showed the same value. The main thread gets
the reserve from the link flag, and threads created without a size (such as
`std::thread`) inherit it. The session worker requests the same reserve
explicitly, because `std::thread` cannot choose one.

The reserve is needed because session tools nest. For example, configuring
a component through the parametric graph calls `AgentSession::dispatch` three
times: the component tool, its feature preview, then the component command
that serializes the document. Headless self-tests run that chain on the main
thread beneath `WinMain`. Interactive Studio runs it on the session worker,
which previously had the same 1 MiB default.

The `studio-parametric-graph-ui` stack overflow was recorded on MSVC 14.44
builds; no MSVC toolchain was available for this change, so MSVC frame sizes
were not measured. On a local llvm-mingw Release build:

- The unmodified 1 MiB build passes the test.
- With the executable's reserve patched to 320 KiB, the test overflowed
  (`0xC00000FD`). With 384 KiB it completed its UI checks.
- The debugger trace at the overflow contained no recursion, only the fixed
  dispatch chain above.
- The largest frames were `WinMain` (about 81 KiB, because many self-tests are
  inlined into it) and `AgentSession::dispatch` (about 50 KiB each, three
  times).

The test now reports the main thread's committed high-water mark. It fails if
either thread has less than the configured reserve, or if the high-water mark
exceeds half of the reserve. A build with larger frames therefore reports its
measured depth instead of passing close to an overflow. With the 8 MiB
reserve, the same build reported a 368 KiB high-water mark. Whether an MSVC
build stays within that limit has not been verified.

`WinMain` still contains most Studio self-tests inline. Its frame therefore
sits under every chain that a headless test runs on the main thread, and under
the tools that interactive Studio runs on the main thread. In the llvm-mingw
build that frame is about 23% of the measured 368 KiB, and the three dispatch
frames are about 41%.

Native tessellation now uses OCCT's parallel mesher with a bounded thread pool.
The default is up to eight logical threads; `ARK_NATIVE_THREADS=1` selects the
serial control and values through 64 can explicitly set a budget.
`kernel_info` reports the actual budget and exact-geometry backend. The mesher
constructor already performs its operation, so ARK no longer calls it twice.
The [OCCT mesher API](https://occt3d.com/dev/doc/refman/html/class_b_rep_mesh___incremental_mesh.html)
documents that behavior and its parallel option.

GPU work already includes resident OpenGL display buffers, material shading,
occlusion, instance transforms and optional CUDA packed-mesh transforms.
CUDA does not make a mesh into authoritative CAD geometry. The automatic
transform path now calibrates CPU/GPU time in three size classes, including
host/device transfers, and requires exact output parity and at least a 5%
measured advantage before selecting CUDA. It caches the choice per process;
`compute_info` exposes the measurements. `compute_benchmark` still reports the
forced GPU measurement even when automatic dispatch selects CPU.

This follows NVIDIA's guidance to evaluate complete data movement and keep
data resident when possible, rather than comparing isolated kernel timings.
[CUDA best practices](https://docs.nvidia.com/cuda/cuda-c-best-practices-guide/index.html)
also explains why smaller operations may stay faster on the CPU.

For further acceleration, finer dependency tracking and repeated resident
GPU pipelines are better candidates than moving arbitrary B-rep operations to
CUDA. [CUDA Graphs](https://docs.nvidia.com/cuda/cuda-programming-guide/04-special-topics/cuda-graphs.html)
can amortize repeated launch setup, but are not implemented here: variable CAD
topology and host/device transfer costs need measured workloads first. Current
evidence must not be described as a general GPU CAD kernel or a guaranteed
whole-model speedup. Appearance-only edits already avoid geometric recomputation;
frame density dependencies deliberately force refresh so mass cannot go stale.

On the integrated vehicle, three runs per setting measured a recompute median
of 1788.28 ms with one native thread and 1295.96 ms with eight, with identical
frame mass and volume. The 2M-vertex CUDA control produced identical coordinates
but transfer-inclusive timing favored CPU on the RTX 4080 SUPER in this run;
the calibrated automatic path selected CPU. Measurements are workload-specific.
