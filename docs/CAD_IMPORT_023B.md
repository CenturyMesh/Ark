# Exact exchange import and Arkitfy — 023B

This work addresses B.02, B.03, B.05 and D.02 within a local authoring scope.
Their wider roadmap milestones remain **Parcial**. Generic CAD import is
separate from the specialized planar tube `import_step` workflow and component
library STEP selection. It appends to the existing document.

Studio opening or dropping STEP/IGES offers **Just import** or **Arkitfy**.
Just import retains a portable immutable exact reference. Arkitfy imports the
same reference and starts an agent reconstruction task. Choosing Arkitfy
authorizes reconstruction; import/recognition alone never reports a finished
parametric conversion or recovered source history.
An idle configured connector receives that task; when settings are incomplete
or a task is running, the reconstruction prompt remains in the actual Assistant
input for explicit continuation.

## Tools and package

`cad_inspect{path}` reads `.step`, `.stp`, `.iges` or `.igs` without modifying a
document. `import_cad{path,mode:"import"|"arkitfy",expected_revision,name?}`
publishes an atomic `native_solid` with `native.op:"imported_cad"`.
`cad_inspect{id}` reinspects its embedded source after the original file moves
or disappears. `cad_inspect{id,compare_id}` compares an exact reconstructed
candidate with that source in their feature coordinate frames.

The version-1 `ark-source-cad` package stores the source format, filename,
original file bytes and SHA-256, exact OCCT B-rep bytes and SHA-256, topology
census and unit evidence. It has no live dependency on an external source path.
The input file is copied into a private snapshot before translation. Subsequent
evaluation reproduces the exchange transfer from the embedded bytes and requires
the exact serialized B-rep digest to match. This verifies the complete trimmed
surface representation rather than inferring surface identity from area/bounds.
The document uses version 38 because it stores this new source recipe; the
release identity itself does not select a document format.

Every transferable root must succeed. The importer retains the whole transferred
shape, including multiple solids and free surface faces. It validates the B-rep,
positive-volume solids and finite trimmed face domains. It includes invisible
IGES geometry. Failed roots, invalid/unbounded B-reps, curve/point-only files,
unsupported/custom units and resource limits reject explicitly without partial
publication. Bounds must be finite within 1e8 mm and surface area must be finite
and positive. Source files are bounded to 64 MiB and inventories to 16,000 faces,
10,000 solids and 10,000 roots; existing native selection/document limits can
reject larger complete packages. No body is silently discarded.

STEP transfers explicitly target millimetres and retain the declared length-unit
names. IGES targets millimetres, retaining the source unit flag/name, conversion
factor and declared model scale. Geometry is translated by OCCT; annotations,
colors, product assembly metadata and the original CAD feature history are not
reconstructed by this generic geometry contract.

## Reconstruction evidence

Read-only recognition tests axis-aligned boxes and single analytic cylinders
against exact primitive candidates. It records source body index, dimensions,
rigid placement, Boolean symmetric difference, area error and bounds error.
Recognition and comparison use OCCT non-destructive Boolean operations; the
source B-rep is frozen before inspection to preserve its exact original transfer.
Only a passing candidate receives `measured_primitive_candidate`. Arbitrary
solids and surfaces remain valid exact references with an explicit unresolved
reconstruction status. Cylindrical face observations do not invent hole intent,
thread specifications or a manufacturing route.

The Arkitfy task asks the agent to create meaningful datums, named driving
parameters, driven sketches and native feature dependencies; to reconstruct each
solid independently; and to use hole/pattern/finishing operations only when
measured geometry supports them. All writes retain `expected_revision` and
preview/atomic publication. It must compare a whole-source candidate before
suppressing the original reference, retain that source/history, and count each
physical body once. Incomplete candidates remain suppressed with the exact source
active. Focused remains the default; this feature introduces no turn/token budget.
Drawing creation is excluded unless explicitly requested.

Closed-solid `compare_id` evidence uses both exact Boolean differences, matching
solid census, area and bounds. Its fixed tolerance is 1e-6 mm for bounds,
`max(1e-6, abs(source_volume)*1e-8)` mm³ for symmetric difference, and
`max(1e-6, source_area*1e-8)` mm² for area. Source and candidate hashes identify
the measured geometry. Free surface/trim equivalence is explicitly unchecked;
matching scalar properties cannot prove it. A recognition proposal or an
equivalence result does not qualify engineering intent, manufacture, cost or
general design quality.

## Local validation corpus

`tests/cad_import_tests.cpp` generates its own exact fixtures, without user CAD:
multi-body STEP containing a translated box and cylinder; mixed solid/free-face
STEP; trimmed surface IGES; millimetre and inch units; unsupported arbitrary
primitive recognition; portable `.ark` restoration after original source removal;
corrupt source, stale revision and source/B-rep tampering, including a replaced
B-rep with a recomputed hash. It also checks explicit refusal to qualify surface
equivalence from scalar properties. Record the actual build/test result in the
iteration verification receipt; this corpus is a local proof, not an external
product integration or manufacturing qualification.

On 2026-10-14 the Release native build completed and `cad-import-023b` passed
42 checks in 1.08 seconds. This includes the real revision-checked tool and
portable document restoration. The early strict-transfer failure was traced to
recognition mutating shared source representation data, then fixed by freezing
the transfer before inspection and using non-destructive Booleans. A separate
census failure caused by canonical JSON key ordering was fixed with typed field
comparison. These failed attempts are part of the validation history.

`src/view/cad_import_selftest.inl` exercises the actual import choices, real
`WM_DROPFILES`, cancellation, portable restore and offline Arkitfy staging.
Its native Assistant input uses the same real EDIT control as ordinary startup;
native control pixels are captured separately from OpenGL. This fixture sends
no provider request and therefore does not prove a live agent's reconstruction
quality or completion. The integrated Release run on 2026-10-14 passed
`studio-cad-import-ui` in 2.51 seconds and reran the 42-check backend suite
successfully in 1.09 seconds. The actual Just import/Arkitfy dialogs, STEP and
IGES viewports, and staged native Assistant input were visually reviewed: mode
and name controls, buttons, source selection, task text and deferred-configuration
status were readable and unclipped. The fixture also verified cancellation,
zero provider traffic and portable restoration after deleting synthetic sources.
