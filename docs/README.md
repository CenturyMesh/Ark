# Documentation

Start with the [project README](../README.md) for build and launch, then the
[handbook](HANDBOOK.md) for practical workflows. This index separates current
contracts from dated evidence so that a historical result is not mistaken for a
current release claim. [release.json](../release.json) owns application identity.

## Start here

| Guide | Read it when |
|---|---|
| [031B modeling and agent contracts](iterations/031B.md) | You need advanced solids, constrained surfaces, sweep laws, triangle edits, rigid condensation or sampled cable/pose inspection |
| [031B candidate refresh](CANDIDATE_REFRESH_031B.md) | An independent peer commit made a retained agent proposal stale |
| [030D tools and viewport agents](PRODUCT_TOOLS_030D.md) | You need Light up, Wood, nets, vents, live activity or the incremental binary loop |
| [030D realtime pipeline](REALTIME_030D.md) | You need local agent/tool/viewport ownership, queue scheduling and matched latency evidence |
| [030D team outcomes and blockers](TEAM_OUTCOMES_030D.md) | A team requests input or stops; distinguish declared outcomes, rejected tools and actual interruption |
| [030D verification](VERIFICATION_030D.md) | You need frozen-source hashes, native/core/UI evidence, retained failures and distribution limits |
| [029B scope and verification](iterations/029B.md) | You need the current A/B implementation boundaries and fresh local evidence |
| [Trust foundation](TRUST_FOUNDATION.md) | You need recovery, regression corpora, performance receipts or release inventories/SBOM |
| [History and configuration authoring](CAD_AUTHORING_FOUNDATION.md) | You need stable-ID reorder/rollback, configurations, selective evaluation or external-source pins |
| [Configuration workspace](PRODUCT_CONFIGURATIONS_UI.md) | You need the Studio controls for stored parameter and visibility recipes |
| [029B native performance](PERFORMANCE_029B.md) | You need viewport reuse, evaluation context and matched native timing evidence |
| [029B sketch assistance and accessibility](SKETCH_ASSISTANCE_029B.md) | You need explicit conflict alternatives, solver bounds or native DPI/focus controls |
| [029B surface repair](SURFACE_REPAIR_029B.md) | You need exact bounded orientation repair and difficult geometry evidence |
| [029B assembly scaling](ASSEMBLY_SCALING_029B.md) | You need selective memory/I/O and configuration/pin ownership in isolated workspaces |
| [023B test guide](AUTHORING_023B_TESTING.md) | You are testing STEP/IGES import, Arkitfy, functional holes or isolated design containers |
| [023B Holes Orchestrator](HOLES_ORCHESTRATOR_023B.md) | You need parametric holes, thread preparation, dowels or retaining-ring grooves with published nominal sources |
| [023B CAD import](CAD_IMPORT_023B.md) | You need portable exact STEP/IGES references and measured reconstruction checks |
| [023B design containers](DESIGN_CONTAINERS_023B.md) | You need scoped editing in another document or ARK instance and guarded integration |
| [022B test guide](AUTHORING_022B_TESTING.md) | You are testing existing-solid resize, background previews, vertex pulls, patterns, advanced sheet metal and drawings in the viewport |
| [022B integrated drawings](DRAWING_VIEWPORT_022B.md) | The user has explicitly requested drawings for parts/assemblies, or you want to use the manual sheet controls |
| [022B component patterns](SPECIALIZED_PATTERNS_022B.md) | You need persistent sketch, curve, inheritance or chain placement recipes |
| [022B sheet programs](ADVANCED_SHEETMETAL_022B.md) | You need exact miter, contour, corner, rip, tab/slot or bend-table authoring within its measured domain |
| [021B direct extrusion](VIEWPORT_DIMENSIONS.md) | You need a source handle, reverse/symmetric depth or exact up-to-face bound |
| [021B technical drawings](DRAWINGS.md) | You are authoring a revision-bound sheet with views, sections, dimensions and tolerances |
| [021B DFM proposals](DFM_PROPOSALS.md) | You need a concrete before/after candidate under the same declared process |
| [021B snapshot camera](SNAPSHOT_CAMERA.md) | An agent needs a Z-up or explicitly oriented visual check |
| [020B direct dimensions](VIEWPORT_DIMENSIONS.md) | You are dragging an exact sketch extrusion or editing source dimensions on a solid |
| [020B agent DFM](DFM.md) | You are screening a declared process and need versioned rule evidence and limitations |
| [019B authoring controls](AUTHORING_019B_TESTING.md) | You are testing image intents, optional dimensions, direct edits, Skeleton code and collision feedback |
| [018B draft controls](AUTHORING_018B_TESTING.md) | You are testing editable sketches, construction geometry and varying sweeps |
| [Handbook](HANDBOOK.md) | You want a walkthrough of editing, assemblies, agents, BOM and export |
| [Connecting agents and accounts](CONNECTING.md) | You need ChatGPT authorization, API configuration or an external MCP connection |
| [Native build](NATIVE_BUILD.md) | You are building with MSVC/OCCT or configuring native desktop checks |
| [Contributing](../CONTRIBUTING.md) | You are reporting an issue or preparing a branch and pull request |
| [Sponsorship](SPONSORSHIP.md) | You want to understand funding setup and its current availability |
| [First try](FIRST_TRY.md) | You want a small editable-part task and a structured way to report friction |
| [Launch materials](launch/README.md) | You need announcement drafts, first-use feedback and local traffic snapshots |
| [Roadmap A–G](ROADMAP.md) | You want planned features, current status and acceptance gates |
| [Development rules](../AGENTS.md) | You are changing release identity, contracts, geometry or distribution |
| [CAD agent protocol](AGENTS.md) | You are using or extending agent tools |
| [Security policy](../SECURITY.md) | You need to report a vulnerability or understand sensitive-data handling |

## Studio and CAD

| Topic | References |
|---|---|
| Commands, selection and display | [Native Studio](NATIVE_STUDIO.md), [interaction](STUDIO_INTERACTION.md), [graphics](GRAPHICS.md) |
| Sketches and references | [Parametric sketch](PARAMETRIC_SKETCH.md), [exact projection](EXACT_PROJECTION.md), [measurement/units](MEASUREMENT.md) |
| Solids, surfaces and meshes | [Surfacing](SURFACING.md), [native 0.9 scope](ROADMAP_09.md) |
| Assemblies and mechanisms | [Mechanical contract](MECHANICAL.md), [manipulation](ASSEMBLY_MANIPULATION.md), [motion](MECHANICAL_MOTION.md), [bicycle transmission](BICYCLE_TRANSMISSION.md) |
| Sheet, tubes and frames | [Sheet metal](SHEET_METAL.md), [source STEP](SOURCE_STEP.md), [frame/sheet operations](FRAME_SHEET_OPERATIONS.md), [notch workflow](NOTCH_WORKFLOW.md), [bend workspace](BEND_WORKSPACE.md) |
| Materials and physical reports | [Rendering/materials](RENDER_MATERIALS.md), [engineering tools](ENGINEERING_AGENT_V2.md), [engineering modules](ENGINEERING_MODULES.md), [manufacturing intent](MANUFACTURING_INTENT.md) |
| BOM and components | [BOM](BOM.md), [component library](COMPONENT_LIBRARY_ARCHITECTURE.md), [BOM schema](schemas/ark-bom-v1.schema.json) |
| Exchange and persistence | [Exchange](EXCHANGE.md), [formats](FORMATS.md), [document container](DOCUMENT_CONTAINER.md), [persistent iterations](ITERATION_HISTORY.md) |
| Example walkthroughs | [KITE showcase](KITE_SHOWCASE.md), [suspension](SUSPENSION.md), [example documents](../examples) |

A contract can include historical context. Prefer its current operation descriptions
and discovery responses over an old version number, tool count or example build path.
The current native build directory is `out/native-release`.

New authoring contracts: [typed sketch entities](SKETCH_ENTITY_AUTHORING.md),
[construction datums](CONSTRUCTION_DATUM_AUTHORING.md), [spatial constraints](SPATIAL_SKETCH_CONSTRAINTS.md)
and [team write windows](AGENT_WRITE_WINDOWS.md).

019B adds [image authoring](IMAGE_AUTHORING.md), [native Skeleton code](SKELETON_AUTHORING.md)
and [indexed mesh edits](MESH_DIRECT_AUTHORING.md), plus
[helix, surface joining, loft controls and collision evidence](PARAMETRIC_GEOMETRY_019B.md).
Use the [019B manual test guide](AUTHORING_019B_TESTING.md) for the isolated preview
and synthetic authoring example.

## Agents and architecture

| Topic | References |
|---|---|
| Provider setup | [Assistant](ASSISTANT.md), [connectors](AI_CONNECTORS.md), [live Studio MCP](CODEX_CONNECT.md) |
| Team use and ownership | [Studio team controls](MULTI_AGENT_STUDIO.md), [team architecture](AGENT_TEAM_ARCHITECTURE.md), [runtime recovery](AGENT_RUNTIME_RECOVERY.md) |
| Queries, math and transactions | [Automation](AGENT_AUTOMATION.md), [object context](OBJECT_CONTEXT.md), [engineering tools](ENGINEERING_AGENT_V2.md) |
| Images, failures and evaluation | [Agent vision](AGENT_VISION.md), [failure replay](FAILURE_REPLAY.md), [activity/feedback](AGENT_ACTIVITY.md), [benchmark](AGENT_BENCHMARK.md) |
| Execution and usage | [Compute architecture](COMPUTE_ARCHITECTURE.md), [usage/pricing](USAGE_PRICING.md) |

External MCP tools and in-app model credentials are separate routes. Model access,
local resources and costs depend on the selected connector. A successful offline
protocol test or catalog fetch does not establish live inference quality.

## Release history and evidence

| Record | Purpose |
|---|---|
| [Validation](VALIDATION.md) | Suite and desktop validation boundaries, including historical CI limitations |
| [Iteration notes](iterations) | Changes, relevant roadmap IDs, observed results and limitations for each iteration |
| [015B record](iterations/015B.md) | BOM workspace redesign and the local native/UI/package evidence recorded for it |
| [015B verification](verification-015B.json) | Machine-readable receipt for that historical iteration |
| [Native 0.9 verification](verification-0.9.json) | Earlier native geometry, readback and Studio/MCP evidence |
| [Complex modeling audit 005B](COMPLEX_MODELING_AUDIT_005B.md) / [006B](COMPLEX_MODELING_AUDIT_006B.md) | Scoped modeling and agent capability audits |
| [Live workspace delivery](LIVE_WORKSPACE_DELIVERY.md), [workspace motion](WORKSPACE_MOTION_VALIDATION.md), [team/library](TEAM_LIBRARY_VALIDATION.md) | Dated integration evidence |
| [Assessment](ASSESSMENT.md), [native sheet assessment](STAGE_NATIVE_ASSESSMENT.md) | Historical engineering criteria and scoped findings |

Verification files are dated receipts for the named runtime, commands and scope.
They are not a claim that a different build, all geometry or manufacturing has
passed. Local checks, hosted CI, deployment and physical qualification are distinct.

## Distribution and licensing

- [Apache-2.0 source license](../LICENSE) and [NOTICE](../NOTICE).
- [Third-party notices](THIRD_PARTY_NOTICES.md): dependencies retain their own terms.
- [Windows updates](UPDATES.md): verified staging/rollback; online channel qualification
  remains separate.
- [Licensing and distribution](COMMERCIAL_STRATEGY.md): source terms, dependency
  notices and distribution boundaries.

## Historical snapshots

The previous README and Portuguese roadmap audit are retained for provenance:

- [README through 011E](history/README-through-011E.md).
- [Roadmap audit of 12 October 2026](history/ROADMAP-2026-10-12.md).
- [Native 0.9 roadmap](ROADMAP_09.md).

Historical records retain dated ARK technical evidence. Internal commercial
planning and external-project snapshots are omitted from the public documents.
No user drawing or session has been added by this documentation reorganization.
