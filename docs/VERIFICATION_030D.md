# 030D local verification

This record covers **A.01/A.03/A.04/B.01/B.02/B.03/C.01/C.03/D.02/D.04/E.01/F.03**
for the bounded [030D contract](PRODUCT_TOOLS_030D.md). Broader roadmap milestones
remain **Parcial**. Runtime identity is **030D / 0.9.31** from `release.json`.

This is the initial feature-freeze record. Subsequent local communication work
is recorded separately in the [realtime comparison](REALTIME_030D.md) and its
[source/artifact receipt](verification-030D-realtime.json); do not attribute the
initial executable hashes below to that later runtime.

## Source and artifact custody

The runtime source was frozen at
`d841714c526c35a76b8c909b40e4bff0e3e629bd`, based on the pending 029B source
`641c488922fa8feca080d24aabd98726a68c40b4`. The development source digest is
`e906637510ace8e22fdd7aa1ea1f4906ddc63b78b7d664ab0b5de991d9c64cdd`.

[The receipt](verification-030D.json) retains actual executable SHA-256/size,
commands, scopes and inspected capture hashes. Studio, MCP, native worker and
the focused recipe test executable matched those hashes after the complete
suite. Subsequent verification-document changes do not change that runtime source.
No local user design, profile or credential is a release input.

## Observed checks

| Check | Observed outcome | Scope |
|---|---|---|
| Windows native Release and Studio | **153/153 passed**, 259.32 s | `ctest --test-dir out/native-release -C Release --parallel 2 --output-on-failure --output-junit regression-030d-d841714.xml` |
| Portable-core product recipes | **1/1 passed**, 0.14 s | OCCT OFF; authored nets, lighting persistence, revision rejection, binary `.ark`, undo/redo, container source42 and read-only Wood query batching |
| Native product recipes | Passed within the full suite | Measured Wood groove and rectangle/circle/slot vent volumes; associative rectangular/circle/slot sheet openings; hidden occurrence quantities; face-bound light and atomic negatives |
| Studio product controls | Passed within the full suite | Actual controls create net/panel/beam/grille, joinery, assembly placement and LED effects; inspected pixels; hidden emitters, independent bloom controls, VAO/buffer custody, real busy preview, occurrence-ID separation and created-node receipts |
| Studio–MCP integration | Passed within the full suite | Real local live session: retained preview, prepared apply, sketch/mesh inspection, save, undo, New and disconnect; transport timeouts were unchanged |
| Incremental binary loop | Passed | Warm **no-change** target build 2049 ms and the focused recipe check 1141 ms, with source digest before/after and runtime identity/hash checks |

The existing native CMake cache had `ARK_TEST_STUDIO_LIVE=ON`; enable that option
when reproducing the 153-test scope on a desktop with OpenGL 3.3. The source was
configured using `cmake --preset native` and built with
`cmake --build out/native-release --config Release --parallel 4`.
The core directory was configured with `ARK_WITH_OCCT=OFF`, and only
`ark-product-tools-030d-tests` was rebuilt and run in that scope. This is not a
complete portable-core or Linux qualification claim.

Run `scripts/dev-build.ps1 -Target Both -Verify` for the measured development
loop. Its named focused test is distinct from the complete suite. These timings
are observations on this machine; a faster accepted design, inference latency
or an equivalent-work speedup has not been established.

## Retained unsuccessful attempts

The initial test compilation used incorrect mesh-bound and mutable-JSON helpers;
these were corrected. Existing discovery/future-format assertions were updated
from 95 tools/source42-as-future to the actual 96 tools/source43-as-future. A UI
fixture omitted `expected_revision` on `doc_new`; rejection preserved the
previous source, and the fixture was corrected.

The first integrated checkpoint passed 152/153 and exposed a live-preview crash.
An isolated retry and an off/on presence diagnostic reproduced it only with the
overlay enabled. The overlay had left its VAO/buffer active for busy-frame
controls. Explicit caller-state restoration and new VAO/buffer/busy-preview
regressions corrected the observed failure. A later sequential checkpoint passed
153/153, followed by the final frozen-source run above.

Inspected captures also exposed selection color masking narrow LED strands.
The selected emitter halo now follows its emission color. Independent-control
checks showed that generic `feature.edit` replaces appearance overrides;
`light_up` now deliberately merges lighting controls and retains prior material,
finish, color and opacity. The generic edit contract remains intact.

Additional circle/slot sheet fixtures initially used spaces in identity prefixes
and were explicitly rejected. The accepted fixtures use valid stable IDs, and
the control documents that constraint. A duplicated early-validation insertion
caused a compile failure and was removed before the accepted build. Geometry
tolerances and transport timeouts were not widened.

The 029B hosted run's Windows jobs failed on MSAA annotation GUID linking, while
its Linux jobs passed. 030D emits the SDK GUID definitions in one translation
unit and links successfully locally. [That historical run](https://github.com/CenturyMesh/Ark/actions/runs/38768029936)
is still a failed run. The subsequent 030D run on
`e9cc4fb606d7e30045fad42da862ce0abb523d2a` passed all four hosted Windows/Linux
native/portable jobs: [actual results](https://github.com/CenturyMesh/Ark/actions/runs/38790716915).
The later realtime source requires its own hosted run.

## Boundaries

Inputs are synthetic scratch CAD. Tool calls and Studio–MCP transport are real;
role-state fixtures include synthetic events. The inspected screenshots do not
establish live-model design quality, remote multiplayer collaboration or lower
agent cost.

Wood covers exact stock and rectangular top-face joinery with declared grain
and a BOM-derived stock list. Structural capacity, anisotropy, allowances and
joint fit remain unqualified. Nets are authored triangle tubes with nominal
sag/weave, without cloth mechanics or checked inter-strand contact. LED emission
and bloom are raster appearance, without calibrated photometry or illumination
of neighboring parts. Vents are openings; formed louvers and airflow are outside
this scope. No Revit/Blender adapter is claimed.

Hosted checks must be associated with their actual source. Fresh relocated
distribution, signing, published OTA transport and production/manufacturing
acceptance remain separate gates. This record
does not qualify distribution or complete the broader roadmap milestones.
