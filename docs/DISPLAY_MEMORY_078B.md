# Display memory allocation — 078B

**A.04 / B.01 — Verificado no escopo local**, on the native Windows Studio and
NVIDIA RTX 4080 SUPER used for the checks.

Display meshes retain the existing exact source hash, shared GPU handles,
float32 positions, triangle order and SNORM16 display normals. Exact B-reps,
picking sources and authored geometry are independent of these buffers.

New uploads first sample available driver memory and reclaim unreferenced
buffers when the incoming allocation needs headroom. Live handles are never
evicted. Unknown telemetry uses a conservative idle allowance; approximate
driver counters do not reject valid geometry or reserve memory in advance.

The source identity is hashed as consecutive existing buffers with Windows
CNG instead of concatenating another complete positions/normals/index string.
The digest remains byte-identical. The existing concatenation fallback remains
available if platform hashing cannot run.

Packed vertices upload in blocks of at most 65,536 vertices (1.25 MiB), rather
than allocating a full second packed mesh in host RAM. The final GPU buffer
size and contents are unchanged. Indices upload directly from their existing
CPU storage. Resources shows active/idle/peak mesh-buffer bytes and peak upload
staging, separately from process-wide driver memory, textures and framebuffers.

A failed allocation preserves an existing handle passed to that upload, frees
unfinished new buffers and leaves an explicit display warning. The Resources
dialog offers a rendering retry. This does not roll back already committed
authoring; a scene containing new parts can remain partly undisplayed until
rendering succeeds. It must never be mistaken for a successful full display.

Verification includes deterministic pressure-policy checks, exact segmented
hash parity, actual OpenGL readback across upload chunk boundaries, display
frame comparisons, live-handle protection and an injected allocation failure.
The injected failure is not evidence of a real driver out-of-memory event.

The real GL fixture passed with 1.8 million source vertices and a maximum
1,310,720-byte packed upload staging buffer. Chunk-boundary bytes, source-normal
hash sensitivity, active sharing, idle eviction and rendering recovery were
checked; captured source/rendering state remained unchanged. This is a staging
bound, not a claim that all process RAM or total GPU memory fits that size.
