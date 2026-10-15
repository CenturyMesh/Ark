# 029B verification

This record covers local Windows x64 Release builds from the 028D base
`a96a7ed0ff8bd8ca6c9a58e0f6b9ec9739392bbb`, with Clang/LLVM-MinGW and OCCT 8.0.1.
The current iteration is 029B / 0.9.30 from `release.json`. No new hosted CI,
Linux execution, real power-loss, remote deployment, signing, independent CAD
recipient or manufacturing qualification is implied.

## Verification inputs and separation

All documents used for the new tests are generated synthetic fixtures. Local
user projects, settings, credentials and running Studio sessions are excluded.
The previous iteration's drawings/Stamp/plane/import/container tests remain in
the suite. Historical receipts from a different source base are not evidence
for this iteration. Failed fresh attempts are retained in local build logs.

## Fresh focused checks

The five new Studio checks passed locally: editor scroll, history, recovery,
configuration workspace and interaction performance. Fifteen retained UI checks
also passed, including canonical drawings, compact viewport/local-model controls,
Stamps and plane/direct/parametric authoring. Captures were inspected for complete
controls, DPI scroll reachability, retained Drawings and configuration actions.
These are local synthetic UI checks, not broad user acceptance.

The native corpus records 84 authoring transactions across nine families:
57 accepted and 27 expected rejections, plus nine periodic-trim guards, with no
unexpected failure in the focused run. Canonical drawing tests include DXF
readback and retained source bindings.

## Initial integrated checks — A.01–A.03, A.05, B.01–B.05

| Scope | Result |
|---|---|
| Native Release, including live Studio UI | 146/146 passed, 407.76 seconds |
| Portable core Release | 70/70 passed, 66.33 seconds |
| Updated package allowlist followup | 2/2 passed, 13.70 seconds |
| Native synthetic authoring corpus | 57 accepted + 27 expected rejections; 9 periodic-trim guards; 0 unexpected failures |
| Canonical DXF drawings | 5 fixtures; exact curve/source accounting and local readback passed |
| Relocated ZIP with isolated profile and SDK excluded from PATH | 13 MCP RPCs; 94 tools; exact box width 26→32 mm, volume 1280 mm³, physical BOM quantity 2, source 40 save/reopen passed |
| Update and distribution contracts | OTA 83 checks; launcher 96; distribution 38; SQLite 86 injected write positions |

The initial local artifact is `ark-windows-x64-0.9.30.zip`, 50,852,153 bytes,
SHA-256 `3c47962edb6176144b6ef68a1fcfe2649eca2ce52a7b3a8776874e8c69baf1c0`.
Its 86 payload-file hashes, three evidence hashes and archive digest were checked
after extraction. The SBOM contains 38 components, including all 34 runtime
binaries. Four supported OSV identity queries returned no records and no query
failures; 34 components remain explicitly unqueried. This establishes neither
vulnerability absence nor publisher signing. No package was pushed, published or
installed over an existing Studio session.

Runtime source was frozen before the final builds. The package's inventory
records its dirty working-tree snapshot at packaging time. This verification
text was finalized afterward; the source-tree digest is an inventory, not an
independent attestation of compiler inputs.

## Measured performance — A.04

The initial baseline 028D and candidate 029B measurements used the same analytic
16/128/512-feature fixtures, authoring hashes, machine, Clang 23.1.3 compiler,
Release `-O3 -DNDEBUG -static` flags and identical measurement-harness bytes.
Both comparison builds used OCCT OFF. Each size retained 21 samples and all
30 median/p95 comparisons. Fresh-process open keeps filesystem caches warm;
it does not measure OS-cold access or native OCCT workloads.

| Feature count | Baseline doc_info p95, ms | Candidate doc_info p95, ms | Native Studio Apply→SwapBuffers p95, ms |
|---|---|---|---|
| 16 | 0.0233 | 0.0348 | 38.00 |
| 128 | 0.1230 | 0.2089 | 108.24 |
| 512 | 0.4464 | 0.8920 | 398.87 |

At this initial checkpoint, the API query regressed approximately 2× at 512
features. The optimization followup below measures the later correction.
Fresh-process load/recompute p95 at that size was 93.3299→94.0299 ms. The Studio
column is a separate final native UI observation with three warmups and 21
measured samples per size; it is not a comparison against a baseline UI.
Its 72 raw attempts had no failures. These measurements do not include physical
input latency, GPU completion, compositor/monitor presentation or manufacturing
workloads.

The [public summary](verification/029B/summary.json) links the scope to retained
[baseline](verification/029B/performance-baseline-028D.json),
[candidate](verification/029B/performance-candidate-029B.json),
[comparison](verification/029B/performance-comparison.json),
[Studio samples](verification/029B/studio-interaction.json),
[native corpus](verification/029B/native-authoring-corpus.json),
[DXF readback](verification/029B/drawing-dxf.json),
[relocated smoke](verification/029B/relocated-smoke.json),
[dependency coverage](verification/029B/dependency-audit.json) and
[artifact provenance](verification/029B/artifact-provenance.json).

## Retained unsuccessful attempts

An initial geometry-test build lacked an include; it was corrected before the
fresh native runs. An early core suite passed 68/70 because two older contract
tests still expected 92 tools; the corrected final suite passed 70/70. A new
container test initially modified a retained journal head directly; its fixture
was corrected without relaxing the ownership guard. The original baseline
fresh-process attempt timed out after 120 seconds; an intermediate retry ran
under uncontrolled background compilation. Both precede the separately recorded
final controlled comparison. The first real package rejected a legitimate 028D
authoring atlas missing from its reviewed allowlist; exact reviewed names and
negative fixtures were added before the successful package. Local logs retain
these attempts; none is counted as a passing final result.

## doc_info optimization followup — A.04

The summary now derives history/evaluation state and active consumed inputs once
per query, then emits rows in authored order. Full loading skips building a
redundant loaded-ID set. `feature_history` also walks the inclusive prefix once.
Response vectors reserve their known sizes; feature rows and the completed list
move into the result instead of making nested JSON copies. No state is cached
across queries, and field names, order, suppression, loading and output semantics
remain unchanged. Geometry, revision guards and document versions are unchanged.

The final confirmation uses 101 samples per size in both the retained pre-change
029B executable and the optimized executable, with identical harness bytes,
machine, compiler/Release flags and fixture hashes. OCCT is OFF in both. These
are synchronous API-query timings; they do not establish display/GPU latency.

| Features | Before median, ms | After median, ms | Before p95, ms | After p95, ms | Observed p95 time reduction |
|---|---|---|---|---|---|
| 16 | 0.0299 | 0.0177 | 0.0378 | 0.0188 | 50.26% |
| 128 | 0.1961 | 0.0998 | 0.2437 | 0.1070 | 56.09% |
| 512 | 0.8573 | 0.3775 | 0.8941 | 0.4073 | 54.45% |

The two intermediate 21-sample variants and their raw receipts remain retained
locally: linear traversal alone measured 0.5878 ms p95 at 512 features, and a
reserved-row variant measured 0.7418 ms. Their medians improved, but their short
p95 observations varied. The final variant additionally removes the completed
feature-list copy; the longer 101-sample pair above is the final comparison.
The initial regression and its older receipts remain intact. A direct speedup
against 028D is not claimed from comparisons with different sample counts.

Fresh verification after the optimization passed the complete portable-core
suite 70/70 in 69.84 seconds and 18 affected native/integration/UI tests in 33.20
seconds. Regression checks cover reordered/inclusive rollback, suppressed
tip/prefix/suffix, selective dependency retention, unloaded consumers, reference
extrusion, empty selection/load-all reset and query read-only behavior.

The updated local ZIP is 50,857,975 bytes, SHA-256
`78db9cd449ee8c9a043b14c4c9988e99e4532dc91cc85d3b3df9b64aff5f00eb`.
Its 86 payload hashes, three evidence hashes and archive hash were checked after
extraction. The isolated relocated smoke passed 13 RPCs, configuration, exact
geometry, physical BOM and source 40 save/reopen. The fresh SBOM audit again
returned no records for four supported queries, with 34 components explicitly
unqueried. The initial ZIP remains preserved; neither artifact was installed or
published. This is the same unpublished 029B development iteration.

See [before](verification/029B/performance-optimization-before.json),
[after](verification/029B/performance-optimization-after.json) and the
[followup evidence](verification/029B/doc-info-optimization.json).
The updated [artifact receipt](verification/029B/artifact-provenance-optimized.json),
[relocated smoke](verification/029B/relocated-smoke-optimized.json) and
[audit coverage](verification/029B/dependency-audit-optimized.json) are separate
from the original artifact's records.

## Selected roadmap followup — A.04, B.01–B.04

The selected followup now implements native viewport reuse, bounded parallel
evaluation contexts, accessible/DPI-responsive native forms, reviewed sketch
conflict alternatives, exact orientation repair, semantic selective loading
and configuration/pin ownership in isolated container v2. These additions retain
the canonical drawing, CAD import, image/Stamp, local-model and visibility
workflows from the 028D foundation.

| Final scope | Observed result |
|---|---|
| Portable core Release, OCCT OFF | **71/71 passed**, 66.05 seconds |
| Native Release with live Studio checks | **151/151 passed**, 403.48 seconds |
| Native authoring corpus | **120 transactions**: 75 accepted, 45 expected atomic rejections, zero failures; retained periodic trim guards also passed |
| Orientation-repair kernel corpus | **21/21 passed** |
| Native performance from extracted final ZIP | **144/144 attempts passed**: 126 measured edits and 18 warmups across three sizes and two modes |
| Extracted ZIP, isolated profile and SDK excluded from PATH | **23 MCP RPCs** and **2 Studio workflows passed**; 95 tools, reviewed sketch preview/commit and source-40/41 geometry save/reopen |
| Payload integrity | **86 payload files**, 38 SBOM components and generated-evidence hashes verified |

At 144 native occurrences, matched full/reuse modes measured scene rebuild p95
**14.3513→4.4482 ms**, rebuilding one part and reusing 143. Apply-to-scene p95
was **149.1689→140.4780 ms**; transactions and native recompute remain part of
that total. The separate 101-sample analytic comparison measured changed-
parameter recompute p95 **72.2077→8.5356 ms** at 512 features. These are equivalent
local synthetic workloads, not customer-project or physical-presentation claims.
See [performance scope and raw reports](PERFORMANCE_029B.md).

The B.04 exact-source fixture retained all 320 authored occurrences and eight
two-solid source definitions while selecting one definition. Pinned file reads
fell from 182,292 to 22,776 bytes; retained native JSON from 1,229,320 to 152,588
bytes. Seven excluded cache body owners were destroyed and reference buffers
were released. These measure derived data and external-pin I/O, not total process
RSS or streaming `.ark` deserialization. See [assembly scaling](ASSEMBLY_SCALING_029B.md).

The initial complete native run passed 150/151 and exposed a real tangent
regression: scaling a nearly-null Jacobian direction displaced a free arc
coordinate outside the existing domain. The corrected solver preserves a unit
coordinate metric, rejects inadmissible trials without clamping and retains
the original residual tolerance. The original workflow and added rotated,
translated and repeated cases passed, followed by the final suites above.
Earlier discovery/version assertions and UI fixture failures were corrected;
all attempts remain recorded. The earlier A4 package passed focused checks but
was not selected for delivery after the full-suite failure.

The final unpublished development archive is
`029b-local-20261015-a5/ark-windows-x64-0.9.30.zip`, **51,116,968 bytes**, SHA-256
`1156171a560bc2f87d74e4a48efe164919ed381f54e989a7ba09ad9ea2946ce7`.
Measured, fully tested and packaged Studio bytes share SHA-256
`676546b3e6d353012989d96aa446eb25c8b96ada43135bec08481c5757588357`.
The final SBOM audit made four supported queries: zero returned advisories and
zero query failures, with **34 unsupported components** still disclosed. This
is not a complete vulnerability-coverage claim. No runtime was installed or
published; the existing open Studio session was preserved.

See the [consolidated receipt](verification/029B/expanded-verification.json),
[artifact provenance](verification/029B/artifact-provenance-expanded.json),
[relocated RPC](verification/029B/relocated-smoke-expanded.json) and
[UI checks](verification/029B/relocated-ui-expanded.json),
[audit coverage](verification/029B/dependency-audit-expanded.json),
[sketch/accessibility contract](SKETCH_ASSISTANCE_029B.md) and
[surface-repair corpus](SURFACE_REPAIR_029B.md).

## Remaining gates

Broader A/B milestones remain **Parcial**. Three-size measured interaction and
API samples do not establish universal latency or a speedup. Source/payload
inventories and SBOM audit coverage do not establish signing or absence of
vulnerabilities. Recovery fault injection does not simulate every filesystem or
power failure. Source 40/41 and DXF receive local contract/readback checks; separate
products and manufacturing workflows need independent end-to-end evidence.
