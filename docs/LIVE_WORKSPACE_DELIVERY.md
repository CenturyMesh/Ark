# Live workspace delivery — 2026-10-10

The continued work is implemented and locally verified. This is a bounded CAD
workflow upgrade, not general manufacturing qualification or feature parity
with SolidWorks, Fusion or CATIA.

| Requested area | Delivered behavior | Current boundary |
| --- | --- | --- |
| Visible multiagent construction | Separate orchestrator, part designer and physics reviewer; independent models/efforts; queued/running/completed steps; sketch drafts and accepted geometry; messages, proposals and bounded continuation | Exact solids appear after native evaluation. Drafts cover supported sketch commands. Pause/cancel and decisions take effect at operation boundaries; an executing geometry operation is not interrupted. |
| Assembly and physics roles | Serialized revision-checked commits, assembly ownership, measured reviewer proposals, and acceptance checks | Beam bending/deflection, ideal Euler buckling and direct weld-throat loading supplement existing mass/load calculations. This is not general FEA, fatigue or a guarantee of production suitability. |
| Large-model navigation | Gesture-local mouse deltas, stable scene orbit, camera-space pan and frame-time smoothing | Automated large-scene navigation checks passed; subjective interaction on every assembly/hardware combination is not established. |
| Sheet metal | Native original-blank jog and linked sketch bend, with ribbon forms, editing/removal, flat/formed inspection and export | One unperforated rectangular blank and full-width straight bends. Arbitrary blanks, multiple internal programs and bend-crossing holes are rejected. |
| Frames and weldments | Round/rectangular straight-member trim/extend, crossing-plane trim, editable exact weld-bead solids, inspection and STEP | Intersecting straight axes for trim/extend; straight triangular fillet beads. Skew/bent-member joints and general weld simulation are outside this domain. |
| Resource and cost display | Local system/Studio CPU and RAM, Studio GPU-engine/VRAM counters, drawing token totals, startup/session price fetch and manual refresh | Remote model compute is not local telemetry. Costs are current API/credit equivalents, not the account invoice or remaining subscription allowance; unavailable counters/rates show N/A. |
| Persistent iterations | Actor metadata, changes, branch-preserving restore, undo/redo after reopening and usage preservation | Caller-declared identity; 512 nodes and 4 MiB logical document limit. Earlier unsaved history cannot be reconstructed. |
| Surface and hybrid modeling | Dedicated Surfaces ribbon; exact extrusion/revolution/sweep/loft/fill/NURBS, face extraction, plane trim, offset, sewing and thickening; explicit mesh sampling | Plane trim retains one connected open face. Mesh conversion is explicitly sampled and loses exact B-rep authority. |
| Native save format | Owned typed binary SQLite container, deduplicated content-addressed payloads, incremental transactions, interrupted-write recovery, legacy reads and explicit JSON exports | Format ownership is not encryption. Smaller incremental writes do not imply lower latency for every document. |

## Verification

- Native build: 49 tests verified. The final full run passed 48/49; the remaining
  manufacturing test compared object insertion order. Its assertion now compares
  the entire deterministic document, and the rebuilt test passed separately.
- Core-only build: 34 tests verified, with the same manufacturing-test correction
  applied after the 33/34 full run; the rebuilt remaining test passed.
- Studio: all 11 self-tests passed, including the integrated assistant/team test,
  sketch, native, rendering, engineering, sheet showcase, context, suspension,
  mechanical motion, dialogs and general UI. Team tests use local injected
  transports and do not establish provider-driven design quality.
- Live public pricing fetch passed: 36 API entries and 9 credit entries parsed.
- The original bicycle loaded, saved to a separate binary file and reopened with
  equivalent authoring: 110 features and 107 assembly nodes. The original SHA-256
  stayed `d66a46d23f6c7315aa37383a407e8255a876724196003030dd038fb4873fff56`.
  The existing Studio process remained open.
- Storage tests include real subprocess interruption with a recovery journal,
  checksums, truncated pages, foreign schemas, rollback, explicit JSON export,
  signature-based loading and persistent undo/redo after binary reopening.
- Local results and measured runtime hashes are in
  [the verification receipt](verification-live-workspace-2026-10-10.json).
  Hosted CI is not part of these local results.

## Measured storage tradeoff

The seven-sample warm-filesystem benchmark uses storage fixtures, without CAD
evaluation or semantic journal replay. For its approximately 1.5 MiB shared-source
fixture, the container occupied 802,816 bytes and a parameter update inserted one
30-byte payload. Median incremental save was 25.91 ms versus 4.32 ms for JSON;
load was 11.78 ms versus 11.90 ms. SQLite uses FULL durability while the old JSON
comparison uses stream flush and atomic replacement. Small files have database
overhead: the real bicycle grew from 59,875 to 135,168 bytes. These measurements
support deduplication and bounded writes, not a general speed or size claim.

## Local runtime

The tested Studio, CLI, MCP and OCCT worker binaries, dependencies and UI assets
were copied to `out/native-release/Release`, with matching executable hashes.
`start-studio.ps1` therefore uses the updated runtime on its next launch. The
already open window still uses its original executable until the user reopens it.
The original `examples/Bike_exemple.ark` remains a local, unmodified user file.

See [team workflow](MULTI_AGENT_STUDIO.md), [frame/sheet domains](FRAME_SHEET_OPERATIONS.md),
[iterations](ITERATION_HISTORY.md), [binary storage](DOCUMENT_CONTAINER.md),
[physics](ENGINEERING_AGENT_V2.md), [cost display](USAGE_PRICING.md) and
[surface modeling](SURFACING.md) for operation contracts.
