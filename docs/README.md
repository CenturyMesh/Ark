# Documentation

Start with the [project README](../README.md) for build and launch, then the
[handbook](HANDBOOK.md) for practical workflows. This index separates current
contracts from dated evidence so that a historical result is not mistaken for a
current release claim. [release.json](../release.json) owns application identity.

## Start here

| Guide | Read it when |
|---|---|
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
