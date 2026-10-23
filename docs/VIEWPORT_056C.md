# 056C viewport quality and representation cache

Milestones: **A.04** responsiveness and measured resource use; **B.01** CAD
viewport quality. Status: **Verificado no escopo** for the synthetic local checks
below. This display work does not qualify **C.04** motion, collisions or manufacture.

The viewport now offers **Off**, **FXAA** and **FXAA high quality** in System
options / Graphics, with immediate application and persisted user settings. The
pass processes the model before UI labels/widgets and leaves CPU geometry and
selection references intact. The previous attempt to select MSAA by calling
`SetPixelFormat` a second time on the same window was removed: Windows permits
that call only once per window. There is no claim of hardware MSAA support from
these controls. [Microsoft pixel-format contract](https://learn.microsoft.com/en-us/windows/win32/opengl/pixel-format-functions).

Contact shadow strength is independently adjustable from 0 to 1. A short
screen-space ray towards the studio key light adds local contact cues. The AO
filter now weights samples by world depth difference, surface normal and screen
distance, and rejects background samples. Existing Off/Contact/Ambient/Horizon
AO modes and radius/strength controls remain available. Shadows depend on visible
depth: geometry outside the view and hidden depth layers cannot cast these
shadows. Progressive path tracing retains its own illumination. These are display
cues, not geometric clearance evidence or a new physical light solver.

GPU display vertices retain their original float32 XYZ positions and uint32
index order. Unit normals use signed normalized 16-bit components: the vertex
stride is 20 bytes instead of 24. The original CPU meshes, B-reps, native face
spans and picking BVHs remain unchanged. OpenGL converts the normalized integer
attribute in the vertex fetch. [Khronos vertex attribute contract](https://wikis.khronos.org/opengl/Vertex_Specification).

Immutable GPU meshes are shared by a SHA-256 identity of their display/source
content. Re-uploading the same content reuses the existing buffers; changes to
positions, indices or source normals invalidate that identity. Reference counts
keep an active resource alive across shared handles. Released resources remain
in an LRU cache up to 32 MiB of idle buffers; active resources are not evicted.
Unloading the scene purges idle resources. Existing unchanged-definition scene
reuse still avoids tessellation, packing and upload entirely. No display cache
is written into a document or used to infer exact geometry.

## Local validation

Windows, LLVM MinGW, verified patched OCCT 8 SDK, NVIDIA GeForce RTX 4070 Ti,
OpenGL 4.6 NVIDIA 610.88; synthetic author-owned geometry only. The dedicated
`studio-viewport-quality-ui` check ran at 1440 × 900 and the same fixture at
1180 × 800. Captures of the model and actual Graphics controls were inspected.

| Measurement | Observed result |
| --- | --- |
| 1,024-vertex test vertex buffer, before / after | 24,576 / 20,480 bytes, queried from the GL buffer |
| Index buffer, unchanged | 23,064 bytes |
| Total vertex + index allocation in that fixture | 47,640 → 43,544 bytes (8.60% less) |
| Maximum packed unit-normal vector error | 0.000024518 |
| Packed vs float32 raster on the same exact assembly | maximum RGB8 channel difference 1 / 255 |
| Off vs FXAA changed pixels, 1440 × 900 | 19,055 |
| FXAA vs high quality changed pixels | 5,647 |
| Contact shadow on/off changed pixels | 3,417 |

The check also verifies shared GPU handles, reuse after release, changed-source
invalidation, active-resource protection during eviction, idle eviction, unchanged
source/revision, zero uploads on an unchanged scene, and real native control
Apply/Cancel, invalid-value rejection and settings round-trip. CPU packing and
GPU upload byte counts are separate from total driver VRAM consumption.

Existing `studio-scene-cache-ui`, `studio-render-ui`, `studio-selection-glow-ui`
and `studio-selection-picking-ui` passed. The first scene-cache regression run
correctly failed because idle buffers survived document unloading; the unload
purge was added and the original test then passed. The original failure log was
retained locally. An additional progressive-renderer check initially exposed an
empty measured crop with the expanded assistant dock; its isolated fixture now
closes the dock before framing, so the test samples the actual model viewport.
That progressive-renderer check subsequently passed, including hidden edges,
camera/projection/visibility reset, runtime path-tracing controls and unsupported
transparency fallback. On its 10,164-triangle fixture, the completed-frame median
was 2.542 ms and p95 3.661 ms; this is a local measurement, not a before/after
performance claim. The final minimum-size quality run also exited successfully
with no competing test instance.

The receipt records 21 completed frames per AA mode using `drawFrame` followed by
`glFinish`. These small fixtures do not establish a general frame-rate gain:
driver clocks, warm-up, CPU UI drawing and other activity affect the samples.
The validated memory reduction applies to the same vertex/index payload;
large-project responsiveness and cross-GPU performance require separate matched
workloads. No private drawing or deployment qualification is included.

Reproduce the dedicated test with `ctest -R '^studio-viewport-quality-ui$'
from a native Studio build with `ARK_TEST_STUDIO_LIVE=ON`. Its
`ui-checks/viewport-quality-ui.png.quality.json` receipt and adjacent PNGs contain
the actual measured values and captures. The minimum-size invocation is
`ark-studio --self-test-viewport-quality-ui --shot quality-minimum.png --size 1180 800`.
