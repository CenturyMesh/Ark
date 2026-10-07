# CPU, GPU and responsive evaluation

The parametric feature history remains ordered so references bind to their
declared producers. Exact B-rep construction and validation run on OCCT CPU
geometry. Studio transfers exclusive ownership of a long session operation
to one worker while displaying a cached, stable frame and an elapsed-time
loading overlay. Other tools wait in the queue; edits resume after completion.
Closing is deferred until the worker joins. The overlay does not invent a
percentage or claim that an uninterruptible kernel call has been cancelled.

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

For further acceleration, dependency-aware invalidation and repeated resident
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
