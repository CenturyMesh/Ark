# ARK agent protocol

049A derived bakes (A.02/C.04, **Parcial**): `motion.bake` now stores a receipt
(the bake fields plus `frames_sha256`/`coordinates_sha256`), never frames. Frames
are derived data from memory, the document's derived section or a re-bake that
must reproduce the receipt; `motion_info{name,frame}` and `visualize_render`
report `frames.source` and `verified`, and an unverified re-bake is never the
recorded evidence. Re-baking no longer grows the journal, so choose fps for the
motion, not for the file size. `MOTION_FRAME_STEP_TOO_LARGE` returns
`frame_step{channel,frame,time_s,max_step_deg,fps,required_fps,fps_limit}`:
bake again with `required_fps`, or slow the channel when it is 0. A 044B-048B
document may still hold inline frames; `history_info.inline_bake_payloads` and
capacity rejections (`derived_bakes`) say so. Only on the user's explicit
instruction call `history_migrate_derived{path,expected_revision,confirm:true}`:
it backs up the saved document beside `path` as `<name>.pre-049A.ark`, rewrites
the journal without the frames (schema 2, new iteration ids, recorded in
`history_info.migrations`) and leaves saving to `doc_save`. Re-read
`history_info` before any `iteration_restore`. See
[derived bakes](DERIVED_BAKES_049A.md).

048B Parametric HUD: only when the user explicitly asks for a HUD, adjustable
dimensions or a parametric design, load `agent_onboard{topic:"hud"}`. Declare
the driving parameters with `param.set{name,expr,hud:{label,min,max,step,unit,group,order}}`,
make every driven dimension an expression of them, run the read-only
`hud_flex_check` (min/mid/max per parameter on a scratch copy) and fix failures,
then report `hud_info`'s list. A value outside the HUD range rejects with
`HUD_VALUE_OUT_OF_RANGE` and the document is unchanged; ARK never clamps an
agent value. A flex pass covers only the sampled values. The human edits the
same parameters from the Studio HUD as ordinary human `param.set` transactions.
See [Parametric HUD](PARAMETRIC_HUD_048B.md).

050B drag chains (B.04/C.04, **Parcial**): when a design needs a cable carrier
(drag chain, energy chain) between a fixed bracket and a moving carriage, use
revision-checked `drag_chain.set{name,link,bend_radius,anchor,moving:{occurrence,
attach,joint},link_count?,travel?}` with the fixed-end pin, run and bend-side
directions, the carriage's moving-end pin and its prismatic joint. Never model a
drag chain as telescoping rigid segments, `min()`/`max()` couplings or one motion
channel per link. Links are native B-rep occurrences driven from the joint
coordinate in every pose, drive preview and bake; bake only the carriage joint.
Inspect with read-only `drag_chain_info{name,q?}`; load
`agent_onboard{topic:"assembly"}` and see [drag chains](DRAG_CHAIN_050B.md).

048B exploded views and Visualize (B.04/B.05/D.04, **Parcial**): author an
exploded view only when the user explicitly asks for one, an assembly image or
an assembly drawing, the same rule as 2D drawings. `explode.set{name,steps}`,
`explode.rename` and `explode.remove` use revision-checked `doc_apply`; each step
moves instances along `+X..-Z` or `[x,y,z]` by a distance in mm or a parameter
expression, and steps play in order over factor 0..1. Exploded positions are
display transforms: B-rep, mates and placements never move, and changing the
factor never changes the revision. A motion channel without `mate` may set
`explode_view` (values 0..1) to animate a view with the joints. Read
`explode_info` (`propose` suggests an uncommitted step list) and render images
with `visualize_render`; images are display tessellation, never geometric
authority. Load `agent_onboard{topic:"exploded_views"}` and see
[exploded views and Visualize](EXPLODED_VISUALIZE_048B.md).

046B profile regions: a sketch with several non-touching closed loops extrudes
(`native_solid` extrude, all end conditions; legacy extrude), revolves
(`revolve_surface` solid:1) and sweeps (section, solid:1) as faces with holes by
even-odd containment. Each feature publishes one solid; choose one of several
regions with `native.regions:[{entity}|{point}]` (format 48). Crossing,
touching, branched, zero-area or self-intersecting loops and open chains
without a selection reject naming the entities. See
[profile regions](PROFILE_REGIONS_046B.md).

046B motion clearance: `motion_info{name,clearance:{pairs|scope,threshold_mm,max_subdivisions}}`
measures exact clearance over a fresh bake, read-only. A stale bake rejects
with `MOTION_BAKE_STALE`; re-bake with `motion.bake` first. `continuous_certified`
is true only when every frame interval is proven contact-free by the joint-chain
bound. Closed loops and transmissions use an assumed envelope and are never
certified. Nothing is persisted. See [motion clearance](MOTION_CLEARANCE_046B.md).

046B exact reads in Studio may run off the UI thread and return
`exact_job{observed_revision,document_changed_since_read}`. Treat the result as
evidence at that revision and inspect the current revision before writing. See
[background exact reads](ASYNC_EXACT_046B.md).

045A protects work at capacity limits. `doc_save` no longer fails when the
optional saved-geometry cache is full: excess entries are omitted and recompute
on open, reported in `saved_geometry`, and a failed save states its reason.
The iteration journal stays a hard limit (512 nodes / 4 MiB, never pruned):
commit receipts add `history_capacity` from 75%, and `history_round_trip` when
an `assembly.drive` only returns to the previous pose. Look at temporary poses
read-only with `assembly_info{drive|sweep}` or `doc_preview` plus
`snapshot{candidate_id,expected_revision}`; commit a drive only for a pose the
design keeps. Studio's live MCP bridge now records committed and failed client
calls in the activity ledger; rate them with `tool_feedback`. See
[045A](iterations/045A.md).

044B adds Skeleton Motion: author a vector motion graph over existing revolute
and prismatic joints with `motion.graph.set{name,graph}` and bake it through the
joint solver with `motion.bake{name,fps}`, both in revision-checked `doc_apply`.
Graphs are analytic and cubic Bezier segments composed by sequence, parallel,
delay, repeat, ping-pong, reverse, time-scale and offset nodes, plus expression
couplings between channels; never keyframe lists. Read `motion_info` for
continuity, ranges and stale status; it never edits. Baked frames are display
poses with no dynamics or collision evidence, and a stale bake must be baked
again. Load `agent_onboard{topic:"motion"}` and see
[Skeleton Motion](SKELETON_MOTION_044B.md).

044B `curve_pattern` also follows a chain of `references[]` or a whole wire
feature (curves, spatial splines, intersection and bridge curves, spline cable
centerlines), with `count` up to 2048 or `pitch_mm`, margins, `skip`, closed
loops without a seam duplicate, and `x_hint`/`frenet`/`surface` orientation.
Use `reference_scheme:"compact"` for large counts. Requests with only the
earlier fields evaluate unchanged. [Curve patterns](CURVE_PATTERNS_044B.md).

044B appearance accepts procedural `pattern` fields (twill, weave, carbon,
leather, brushed, knurl, perforated) without image files, and stamps accept
animated GIF and MP4 assets with `loop`, `speed`, `autoplay` and `start_s`.
`snapshot` draws textures, patterns and surface stamps at `media_time_s`; check
`appearance_surfaces` for media status. Appearance stays non-authoritative for
geometry. [Media and textures](MEDIA_TEXTURES_044B.md).

044B Claude Code connector: inside ARK, Claude Code reaches only the role's
ARK tools through the host-owned SDK MCP server; built-in file, shell and web
tools are disabled. Tool contracts are the same as on every other route.
[Claude Code inside ARK](CLAUDE_CODE_CONNECT_044B.md).

042D: rate activities inside `team_report_task`/`team_finish_goal` with
`ratings` instead of a separate `tool_feedback` round. Use `query_batch` row
`id`, `{"$ref":"id.path"}`, `paths` and `max_bytes` to chain reads in one call,
and `doc_info {detail:"digest"}` with `known_fingerprints` to see only what
changed. Fingerprints detect change; `expected_revision` still governs writes.
Failed reads stay visible but no longer block completion by default. See
[042D](iterations/042D.md) and [the tool layer](AGENT_TOOL_LAYER_042D.md).

041D read receipts include `team_progress.next_action`. Follow the actual
target-specific recovery; repeated coordination evidence is not a new design
checkpoint. A rejected selector is superseded automatically only when later full
native topology proves its original unique predicate. Otherwise a ready
`read_resolutions` report requires your explicit decision that the observed
semantic reference serves the design intent. Preserve the blocker when it does
not. Historical projected progress never authorizes a write at an old revision.
From 047D, a rejected read is also superseded automatically in one case: a
committed `doc_apply` or `team_apply_step` that removes the inspected feature
with a numeric `feature.remove` id. That rejection can no longer be answered.
The committing receipt lists it under `team_progress.read_resolutions` with
`resolution_source:"subject_removed"`. Rejected reads of surviving features
remain.
External `studio_team_diagnostics` is read-only and is not a model tool.
See [041D contracts and evidence](iterations/041D.md).

039D adds local persistent semantic/text memory. Studio supplies bounded startup
data from the current project (including labelled scratch episodes) and curated
shared ARK knowledge, separate from static onboarding instructions. Use
`memory_context` for relevant recall, `memory_record` for attributed outcomes,
promotion and feedback, and `memory_maintain` for selected consolidation/reindex/
compression with observed memory revisions. Native CAD writes enqueue small
scratch episode receipts in a background worker; queue acceptance is not durable
storage. Historical memories never authorize writes or satisfy CAD completion.
See [memory contracts](PROJECT_MEMORY_039D.md).

038B preserves successful query-batch rows when another row requests an absent
field. Use the returned observed result, available fields and recovery arguments;
do not repeat an unchanged failed projection. A corrected same-subject read may
explicitly resolve an earlier selector failure through the documented
`team_report_task` `read_resolutions` contract. Only a later successful receipt
at the current revision can support that resolution; it never waives a write,
preview or completion gate. See [038B evidence](iterations/038B.md).

037D preloads static overview/sketch/solids contracts for Studio writers and
retains loaded guides across request compaction. For an empty document in
Parallel mode, the lead commits initial editable geometry before other writers
start their retained assignments; read-only reviews may run independently.
Released writers still inspect current revision before editing. Studio allows
one coherent atomic step of up to 128 commands, 32 new features and 64 placements
within role scope. External core defaults remain eight. See
[037D evidence and real-model limits](iterations/037D.md).

035B adds natural `plastics` and `vendors` onboarding, explicit spline/ribbon
cable recipes and shared graph navigation for endpoints, PCB component/net
labels and source/artifact links. See [cables](ELECTRONICS_035B.md),
[Plastics](PLASTICS_035B.md) and [Approved Vendors](APPROVED_VENDORS_035B.md).
`library_vendors` reads/updates local source policy; its expected revision belongs
to the policy, separately from CAD and component-record revisions. Public
discovery/download, a browser account and an authenticated API adapter are
distinct states. Preserve observed links and actual STEP bytes/hash; never
invent product matching, credentials, electrical continuity or manufacture.
Only the new cable semantics conditionally require document source45.

034D adds read-only `context_graph` discovery over saved authoring and attributed
activity. All team roles use the same host index; a committed edit invalidates
it before the next query. Query a small neighborhood, then inspect selected
objects/current revision before editing. Scope/fingerprints omit unchanged node
facts without granting write authority. Historical complaints retain their
author and do not prove an unresolved current defect. See
[context graphs](CONTEXT_GRAPHS_034D.md) for code lookup, local Obsidian export,
safe request deduplication and measured limits.

033D adds actual-allocation preview receipts, explicit owned-candidate
`refresh_candidate:true` publication, compact team defaults and measured request
context. Read [033D contracts](iterations/033D.md). Selected read-only assignments
and `bom_inspect`/`bom_review` `ids` support specialists without satisfying the
lead's global BOM completion gate. `release_reservation:true` on a successful
`team_apply_step` cleans up only that owner's window. Revisions, dependency checks,
role permissions, DFM acceptance and atomic rejection remain mandatory.

032D keeps the existing tool set and adds [compact response and coordination
contracts](AGENT_CONTRACTS_032D.md). Load `agent_onboard{topic,detail:"guide"}`
once per needed topic. Read `doc_info{detail:"revision"}` for status and selected
`object_context` for live authoring/dependencies. Prefer preview/apply
`detail:"changes"`, native inspection `detail:"properties"`, and
`assembly_info{detail:"result",drive|sweep|measure|clearance}` when only that
operation is needed. Reuse observed revision receipts, preserve atomic writes,
and acquire a reservation only when ready to publish within its explicit TTL.
Expired owner receipts can be released idempotently within their retained epoch;
they never grant write authority or silently renew a reservation.

Read [preview storage](PREVIEWS_032D.md), [saved geometry identity](PERSISTENCE_032D.md),
[pattern motion](ASSEMBLY_MOTION_032D.md), [software snapshots](SNAPSHOTS_032D.md)
and [032D verification](iterations/032D.md) for their checked scopes and limits.
See [compact native payloads and shared meshes](GEOMETRY_STORAGE_032D.md) for the
immutable storage and lossless display-channel contracts.
Nonidentity whole-pattern carry frames conditionally require source44. Saved
geometry remains native B-rep, with no STEP or display mesh embedded by this work.

031B extends the existing 96 standalone tools with advanced retained modeling
recipes and compact diagnostics. Read [031B scope](iterations/031B.md),
[solid recipes](SOLIDS_031B.md), [surface/sweep contracts](SURFACES_031B.md),
[mesh operations](MESHES_031B.md) and [sampled assembly/cable inspection](ASSEMBLY_SWEEP_031B.md).
Load the existing `solids`, `surfacing`, `mesh`, `assembly`, `routing` or
`automation` onboarding topic. New recipes conditionally require source43;
older drawings retain their required earlier format. `feature.edit inputs`
requires the observed revision and accepts only unique earlier dependencies.
Its geometry and all dependents re-evaluate atomically.

Use `mesh_inspect{self_intersections:true}` for bounded complete triangle contact
diagnosis and `reference_inspect{reference,detail:"frame"}` for a compact exact
frame. Reference filters belong inside `geometry_query.queries[].filter`.
`library_source_page.links` and `resource_links` expose observed resolved HTTPS
anchors; inferred file kinds do not validate target bytes or qualify sourcing.
`assembly_info.sweep` reports requested sampled poses, never continuous collision
clearance or dynamics. Every rigid mate remains checked after condensation.


030D adds Wood, mesh networks, exact vents and Light up. Load
`agent_onboard{topic:"wood"|"networks"|"lighting"}` and read the
[contracts](PRODUCT_TOOLS_030D.md). `wood_inspect` reports authored stock and BOM
quantities. New persisted recipes conditionally select source42; retain observed
revisions, exact face custody and atomic writes. Emission and spatial agent
markers are viewport display, with no manufacturing or speedup claim.

The [030D realtime pipeline](REALTIME_030D.md) keeps document writes serialized
and revision-bound while yielding normal dispatch to rendering after publication
or an eight-millisecond UI slice. Local measurements separate queue, capture and
worker time. This adds no agent budget and makes no inference-speed claim.

[Team outcome reporting](TEAM_OUTCOMES_030D.md) retains explicit `needs_input`
when a tool was rejected. Review blockers is a read-only UI inspection, without
provider continuation or CAD writes. Unresolved failures still block completed
goal acceptance; cancellation and actual interruption retain distinct states.


029B discovers **95 standalone tools**: the 028D tool set plus read-only
`feature_history{}`, `assembly_authoring_info{}` and revision-bound
`sketch_repair{name,sketch?,expected_revision}`. Read
`agent_onboard{topic:"history"}` before reorder/rollback and
`agent_onboard{topic:"configurations"}` before named configurations, selective
evaluation or external-source pins. Writes share `doc_apply` and its observed
`expected_revision`; invalid dependencies and source drift reject atomically.
History/configuration/loading/pin recipes conditionally require source 40;
scoped container v2 and explicit orientation repair require source 41. The existing canonical drawings,
CAD imports, Stamps, planes and scoped container protocols remain authoritative.
`drawing_export` also accepts `dxf` for a current explicitly requested sheet;
exact projected curves/hatches and source metadata receive local readback checks.
Interoperability with another CAD product and drafting qualification require
separate evidence. See [029B scope](iterations/029B.md).

026D exposes installed Ollama models for the Assistant and individual agent cards.
Local models use the same tool permissions, revision guards and steering protocol.
Tool/vision capabilities are checked from Ollama metadata; declared support is not
a measured design-quality claim. See [local model setup](LOCAL_MODELS_026D.md).

025B adds manual viewport visibility controls and a selected-part/container
drawing shortcut. Opening a drawing view or selecting a source does not authorize
drawing creation; agents still create manufacturing drawings only when requested.
The 024B authoring tools and source-format contracts below remain unchanged.

024B discovers **92 standalone tools**. Read `agent_onboard{topic:"stamps"}`
for portable embedded PNG/JPEG, projected **Stamps**, same-body face appearances
and exact split-line regions. Generate imagery through an actually available
client skill/connector, then import its real artifact using `image_asset`;
image generation inside ARK is not implied by an import tool. Inspect exact
face names and `brep_sha256` before binding, and retain expected_revision.
New image/face/split recipes conditionally require source format **39**.

Read `agent_onboard{topic:"planes"}` for `plane_authoring`: create/offset planes,
move an editable sketch to a named plane, and create exact contour or retained-
side cuts from planes or saved sketch lines. A line defines an infinite section
plane, not a finite kerf. Contours currently require one connected wire.

Users can steer active single/team conversations with new text and images.
Consume new input at the next provider boundary before planning another write;
old queued writes reject at the actual UI execution boundary. Inline snapshots
without `path` are document reads even during a peer write reservation; path
exports retain write guards. `native_inspect{detail:"report"}` provides compact
measured evidence; full topology remains available when binding references.
See [024B evidence](iterations/024B.md), [Stamps](STAMPS_024B.md),
[split lines](SPLIT_LINES_024B.md), [planes and sections](PLANE_SECTIONS_024B.md),
and [manual testing](AUTHORING_024B_TESTING.md).

023B discovers **90 standalone tools**. Use `holes_catalog` and
`agent_onboard{topic:"holes"}` for functional holes: author a parametric
`native_solid` / `holes_orchestrator` feature, with named locations and explicit
source dependencies. Sketch-only holes are for nonfunctional contours. Catalog
nominals do not establish tolerances or manufacturing qualification. For sheet
parts whose flat pattern must remain associative, read the sheet-specific
guidance before choosing the cut operation.

Use `cad_inspect` before `import_cad` for general STEP/IGES references; retain the
exact embedded source. Arkitfy is an agent reconstruction workflow: create native
editable features and compare them against the immutable reference. Recognition
does not establish design intent or universal equivalence. `import_step` retains
its specialized tube workflow. See [CAD import](CAD_IMPORT_023B.md).

`design_containers` inspects assembly groups, checks out a scoped document with
read-only dependencies, and proposes or atomically applies a merge. Bind merge
to the observed parent revision and reviewed child digest; stale source rejects
without silent rebase. See [containers](DESIGN_CONTAINERS_023B.md) and load
`agent_onboard{topic:"containers"}`. These new persisted recipes conditionally
require source format **38**; release identity alone does not change the format.

022B retains **86 standalone tools** and expands their existing authoring
contracts. [Component patterns](SPECIALIZED_PATTERNS_022B.md) add linked
pattern/sketch/curve/chain sources with stable copy slots and expression-driven
recomputation. [Advanced sheet operations](ADVANCED_SHEETMETAL_022B.md) add
bounded exact miter/straight-contour/corner/notch/tab and bend-table geometry.
New source recipes require document **37**; the release identity alone does not
change a document's format. Keep B.01/B.02/B.04/B.05 and C.01 **Parcial** outside
their measured local scope.

Agents create, edit, refresh or export 2D technical/manufacturing drawings
**only when the user explicitly requests that drawing work**. Design completion,
BOM review, DFM and an available template do not authorize drawing writes.
Read-only discovery remains available, and human Studio controls remain manual.
For requested drawing work, call `drawing_inspect{}` first and read `sources[]`.
Each supported part, group/root assembly or occurrence/copy source exposes an
editable `suggested_drawing`; unsupported sources name the reason. Review that
recipe, then `drawing.set` through revision-checked `doc_apply`, inspect the
current result, explicitly `drawing.refresh` after source changes and
`drawing_export` only at the current revision. Read [the drawing contract](DRAWINGS.md).
Source identities, frames and exact curves remain the geometric authority;
SVG is a presentation export. Tolerances require explicit caller input.

Load `agent_onboard{topic:"assembly"|"automation"|"sheet"|"drawings"}` for the
corresponding 022B contract. Human Studio controls use the same editable recipes.
Start open design requests with a coherent editable concept; load specialized
catalog, DFM or process workflows when required by the user's actual task.
Honor explicit processes/materials from the start. Focused remains the default
and Parallel an option, without default turn/token budgets.

For an exact CAD vertex pull, first read `native_inspect{id}.topology.vertices`
and retain its real `vertex.N` name and `source_topology` B-rep digest. Use
`native_solid` with `inputs:[source]`,
`native:{op:"vertex_pull",source_vertex,source_topology}` and source-local
expression displacement `p:{dx,dy,dz}`. The supported domain is a closed
planar-triangle/convex-quad polyhedron; affected nonplanar quads are explicitly
faceted. Curved, holed, complex, stale, intersecting or degenerate cases reject.
Display mesh indices and image pixels cannot identify these exact vertices.

021B discovers **86 standalone tools**. [Direct extrusion](VIEWPORT_DIMENSIONS.md)
retains exact source/limit bodies, [associative drawings](DRAWINGS.md) bind to a
source B-rep and revision, and [DFM proposals](DFM_PROPOSALS.md) remain
revision-bound until explicit acceptance. The [snapshot camera](SNAPSHOT_CAMERA.md)
uses Z-up presets or an explicit caller direction/up pair. Read the scoped
contracts before writing; none of these tools alone qualifies manufacture.

B.05 has a partial associative technical drawing scope: exact orthographic HLR
and isometric views, plane-section contours/hatches, circular centerlines,
projected extents and semantic dimensions bound to the observed part or
explicit assembly occurrence set. Call `agent_onboard{topic:"drawings"}` and read
[the drawing contract](DRAWINGS.md). `drawing.set`/`refresh`/`remove` require
current `expected_revision` in `doc_apply`; stale drawings cannot export.
JSON retains exact derived curves and SVG is a presentation preview. This does
not qualify manufacture or complete B.05.

020B adds source-owned solid dimensions and exact sketch extrusion by viewport
drag. Read [the authoring contract](VIEWPORT_DIMENSIONS.md): double-click edits
the authored expression, and agents use `doc_apply` at an observed
`expected_revision` to add or alter driving dimensions. The DFM v1 rule report
is available through `manufacturing_inspect` for explicitly declared process
reviews; see [DFM rule scope](DFM.md). These checks do not qualify production.

Actionable DFM candidates use `manufacturing_propose` with an observed revision,
current rule ID/version and explicit bounded editable commands. Review exact
before/after evidence and unresolved checks with the user; the retained candidate
requires `accept_dfm:true` for atomic `doc_apply` or same-role `team_apply_step`.
See [DFM proposal contract](DFM_PROPOSALS.md). A passing screen does not imply
functional design acceptance or manufacturing qualification.

019B discovers **83 standalone tools**, including `skeleton_inspect`,
`skeleton_compile` and `collision_inspect`. Source edits and indexed mesh edits
require the current `expected_revision`. Inspect the [019B source and feedback
contracts](#019b-source-and-feedback-contracts) and [manual controls](AUTHORING_019B_TESTING.md).
Image references retain actual pixels, their intent and optional rough dimensions;
the agent must keep reconstruction assumptions distinct from measured geometry.

018B added editable viewport sketches, typed sketch entities, associative datums,
spatial equality constraints and varying-section sweeps. See [018B scope](iterations/018B.md),
[manual controls](AUTHORING_018B_TESTING.md), [spatial constraints](SPATIAL_SKETCH_CONSTRAINTS.md)
and [source contracts](SKETCH_ENTITY_AUTHORING.md). Its standalone discovery listed 80 tools.
In-app teams can use [short write reservations](AGENT_WRITE_WINDOWS.md) after reading
the current revision; reads remain available and stale writes still reject.
Inspect world datum axes before placement/revolve. Web candidates require a real
product-page review; unrelated results and empty pages reject explicitly.


017B added continuous sketches, equation curves, surface projection/trim and BOM
part batch export. Its discovery listed 80 standalone tools. Read
[017B scope](iterations/017B.md) and [manual controls](AUTHORING_017B_TESTING.md).
Use `library_source_search{source:"web",query}` then `library_source_page{url}`
for actual purchased-component evidence; persist real URLs/references in part
metadata. `reference_navigate` discovers adjacent exact faces/edges;
`selection_set` applies live Studio selection at the observed revision.
`bom_export_parts` atomically publishes counted definitions with hashes.


014D adds `bom_review` (76 standalone tools). Before Studio design completion,
inspect every `bom_inspect` item page at one current revision and submit
`bom_review{expected_revision,scope,fastening_review,pending_notes?}`.
Every counted physical definition needs an internal part number and description.
Detailed scope rejects unresolved metadata. Concept scope must disclose pending
fields and is labelled as such by the host. Do not downgrade a user-requested
detailed deliverable or invent sourcing: report needs_input instead.
Review becomes stale after edits. Open **Manage → BOM** for the table/editor.

012D adds `activity_info` and `tool_feedback` (75 standalone tools). Agent
actions return measured activity IDs; submit utility/difficulty self-ratings
and action descriptions before completion, including failed attempts. Record
issues and wished tools/options when relevant. See [project activity](AGENT_ACTIVITY.md).

## Mandatory roadmap and iteration identity

Follow the repository-wide [AGENTS.md](../AGENTS.md) and [A–G roadmap](ROADMAP.md).
The public iteration code is three globally sequential digits plus an uppercase
phase A–G, for example **001A**. The letter is the roadmap phase, **not the month**;
the counter does not reset when a phase changes. `release.json` is authoritative.
Keep the numeric runtime version for Windows/OTA and expose `iteration_code`
alongside it in agent-facing metadata. Identify affected milestone IDs and record
actual acceptance evidence before claiming them delivered. Roadmap proposals
do not authorize license changes. Apache-2.0 was explicitly adopted on 2026-10-12;
see [LICENSE](../LICENSE).

The reusable component library adds nine `library_*` tools, bringing discovery
to 66 document/library/history tools; `object_context` and the three local failure
inspection/export/replay tools bring the total to 70 in `008D`.
Call `agent_onboard{topic:"components"}` for
typed metadata and reuse rules when real standard/purchased components are required.
For that task, search the local catalog first, acquire and
validate original STEP bytes, keep declared metadata separate from native
geometry evidence, then insert using `expected_revision`. See
[library architecture and acquisition](COMPONENT_LIBRARY_ARCHITECTURE.md).
Inserted exact source components use document version 24.

Studio defaults to a focused sequential team: an engineer/orchestrator, a
designer and a physics reviewer, with independent model/reasoning settings
and histories. The engineer authors parts/sketches/materials and assembly/mates
directly, delegating specific components or checks when useful. One conversation
runs at a time; finish a turn after a handoff so that the queued recipient can
run. Parallel mode remains available with up to eight concurrent agents,
including up to six part designers. Document commits remain serialized.
The part designer writes assigned parts/sketches/materials; the physics reviewer uses actual
calculation and inspection tools and sends measured revision proposals. Stable
role IDs preserve existing saved settings. See [team architecture](AGENT_TEAM_ARCHITECTURE.md)
and [Studio team controls](MULTI_AGENT_STUDIO.md).

The team adds `team_send`, `team_context`, `team_propose`, `team_proposal`,
`team_apply_step`, `team_report_task` and lead-only `team_finish_goal` to its
connector schema, not standalone MCP discovery. Report completed/needs_input/
in_progress explicitly with output IDs, evidence and limits. Idle is not a goal
completion report; pending work and unresolved failures prevent completion.
Construction steps are atomic. Studio permits up to 128 commands, 32 created
features and 64 placements within that command bound and the role scope;
external core teams default to eight commands/features/placements.
Related operations can form one coherent batch; one provider round per feature
is not required. Cached previews can commit only through a retained same-role
receipt whose commands/checks and baseline passed the same scope/count validation. Explicit observed `expected_revision` is
required; proposals cannot be silently rebased. Agents retain ownership of their
prepared recipes. After a peer commit, explicitly request
`doc_preview{candidate_id,expected_revision:current}` to refresh a supported
unchanged scope, then inspect and apply the new receipt. The old apply payload
still rejects; refresh never commits implicitly. See
[candidate refresh](CANDIDATE_REFRESH_031B.md). Real queued/start/end events
include step/role IDs and measured revision receipts. Bounded sketch commands
may be shown as uncommitted drafts; they are not evaluated CAD authority or
partial OCCT solids. Native operations publish geometry only after completion.

Studio runs without provider-round or team-turn budgets by default. Optional finite host/test windows yield with retained tool receipts.
Explicit continuation grants a new bounded window and requires successful
current `doc_info` before another write. An SSE response without its terminal
event executes no tools from that response; previous commits remain, and its
unknown token usage is recorded. Existing direct history/load/import calls also
honor `expected_revision` when supplied.

`manufacturing_inspect{id?}` is the 55th document tool. It distinguishes
injection molding, FDM additive manufacturing and machining, separating
caller-declared process settings from exact geometry evidence and unresolved
checks. These process settings use document version 23. Production qualification
is never inferred from a role's completion or a filled checklist.

C.03 adds the versioned [DFM contract](DFM.md) to each component's `dfm` report:
laser/bend/FDM/CNC/weld screens retain exact producer evidence, caller capabilities
and explicit assumptions. Read every rule status and typed recommendation;
`compatible` is a bounded screen, and production qualification remains false.

Mechanical motion uses persistent chain, gear, shaft, rack/pinion and harmonic
cam coupling mates plus read-only `assembly_motion`. Call
`agent_onboard{topic:"motion"}` and see [mechanical motion](MECHANICAL_MOTION.md).
Direct manipulation uses `assembly_info{drive:{driver,mate?,mode?,delta}}` for a
read-only joint-aware preview and `assembly.drive` in revision-checked
`doc_apply` for its persistent pose. Rigid attachments and passive revolute
joints carry the connected moving side. New rigid/revolute mates require
document version 29; opacity and drive retain existing appearance/placement
storage. Call assembly onboarding and see [assembly manipulation](ASSEMBLY_MANIPULATION.md).

For requested manufacturing readiness, declared production processes or fabrication
claims, call `agent_onboard{topic:"manufacturing"}` before authoring those operations.
For an open-ended concept, establish form and interfaces first; unknown production
choices can remain explicitly unspecified. Respect user constraints from the start.
When specifying manufacture, declare each component's fabrication route and inspect
`doc_info{detail:"design"}` for operations, dependencies, sketch consumers and
the originating sheet/frame behind rigid placements. See
[manufacturing intent and discovery](MANUFACTURING_INTENT.md).
Frame trim/extend, crossing end planes and declared exact weld beads, plus
original-blank full-width jog/sketch bends, use document version 25. Call frame
or sheet onboarding and see [frame and sheet operations](FRAME_SHEET_OPERATIONS.md).

The [engineering update](ENGINEERING_AGENT_V2.md) adds `physics_inspect` and
`materials_inspect`, required `doc_apply` postconditions and cached
`doc_preview` candidates. Use `agent_onboard{topic:"physics"}` for physical
units and model limits. A prepared apply refreshes Studio display buffers while
reusing the already evaluated document. Elastic material properties save as
version 20; ordinary density/appearance documents retain their earlier version.

The current API has **73 tools**. BOM/fastener onboarding introduces `bom_inspect`,
`bom_export` and `fastener_recipe`; see [the BOM contract](BOM.md).
Maintain part metadata on final physical definitions, inspect missing fields
and actual quantities, and review mounting hardware for detailed assemblies.
Generic fastener recipes are not supplier catalogue evidence.
`agent_onboard{topic:"automation"}` introduces
`geometry_query`, `math_eval`, `query_batch`, `doc_preview`, `placement_plan`,
`pattern_plan` and `design_solve`. Linked curve and hole patterns save as
document version 19. See [agent automation](AGENT_AUTOMATION.md) for contracts,
examples, numerical limits and execution details. Initial discovery returns a
compact `doc_apply` description; the automation topic retains the full reference.

The current API adds `suspension_inspect` for read-only double-wishbone poses.
Call `agent_onboard{topic:"suspension"}` before authoring its version 18 program.
It returns joint residuals, wheel attitude and ideal spring force; the feature
is a kinematic display model, not an exact B-rep or dynamics solver.
See [suspension](SUSPENSION.md).

The earlier sheet/showcase extension reached **44 tools**. `sheet_inspect` and
`export_flat_dxf` inspect/export authored flat sheets; `stock_preview` returns
nominal straight tube/frame stock; `usage_info` and `usage_record` manage
persisted drawing token receipts. `sheet.opening.pattern` adds bounded
expression-driven copies, and sheet openings support circle/slot/window/polygon.
Usage metadata and slot/window documents use version 17. See
[KITE showcase and counters](KITE_SHOWCASE.md).

The earlier engineering extension brought the API to 38 tools; the latest additions above bring it to 44. New feature types
`construction_frame`, `pcb` and `cable_route` use document version 16.
Call `agent_onboard` with `frames`, `electronics` or `routing` before designing
them. `frame_inspect` and `electronics_inspect` return compact evaluated
evidence; `pcb_export` writes fabrication previews. `export_iges` provides
exact IGES exchange and `export_mesh` accepts `fbx` as well as `obj`/`stl`.
See [engineering modules](ENGINEERING_MODULES.md), [exchange](EXCHANGE.md),
and [CPU/GPU execution](COMPUTE_ARCHITECTURE.md).

The current material/rendering extension adds `agent_onboard{topic:"materials"}`,
persisted materials and appearance, and exact-solid mass/weight/centroid reports.
Use `material.set{name,density?,appearance?}` and `material.remove{name}` in
`doc_apply`; assign `material` or `appearance` through `feature.add/edit`.
Appearance-only batches preserve the geometry cache and report
`geometry_recomputed:false`. `native_solid` also supports `split_plane` and
mirror `p.copy_only=1`. These additions use document version 15 without adding
tools. See [materials and rendering](RENDER_MATERIALS.md) and
[exact split examples](SURFACING.md#exact-plane-splits-and-copy-only-mirrors).

The C++ MCP host reads one JSON-RPC message per line on stdin and writes protocol responses only to stdout. Tools return both `structuredContent` and its JSON text representation, with `isError` set on failures, following the [MCP tool result specification](https://modelcontextprotocol.io/specification/2025-11-25/server/tools). Transport behavior follows [MCP stdio](https://modelcontextprotocol.io/specification/2025-11-25/basic/transports).

Initialize with protocol `2025-11-25`, `2025-06-18` or `2024-11-05`. Each process owns one document. The API is synchronous; a long operation occupies that session. Applications must not call a single `AgentSession` concurrently.

## Recommended modeling loop

1. Call `agent_onboard` for capabilities, current document and revision; request the relevant topic. Use `doc_get` only when full model details are needed.
2. Form a batch of `param.set`, `feature.add`, `feature.edit`, `feature.remove` or `sketch.set` commands.
3. Call `doc_apply` with `expected_revision`. Any evaluation failure leaves the existing document and revision intact.
4. Use `mesh_inspect` and `mesh_raycast` for compact evidence. Request `mesh_get` only when full positions/indices are necessary.
5. Generate a `snapshot` for visual feedback and `doc_save` to preserve intent. Export a mesh only when needed.

```json
{
  "tool": "doc_apply",
  "arguments": {
    "expected_revision": 0,
    "commands": [
      {"op":"param.set", "name":"width", "expr":"60"},
      {"op":"feature.add", "type":"box", "name":"Base", "p":{"dx":"width","dy":"width/2","dz":"8"}}
    ]
  }
}
```

The envelope above is accepted by Studio's command editor and `.arkops` replay. To use MCP, wrap the tool name and arguments in `{"jsonrpc":"2.0","id":1,"method":"tools/call","params":{"name":"doc_apply","arguments":{...}}}`.

Use `created_ids` returned by a batch as inputs to later features. IDs are monotonically allocated within a document and persisted, including its next-ID counter. Inside a batch, `feature`, feature `inputs`, and mate endpoints `a`/`b` accept a unique name or an id. The saved document stores the numeric id. A missing or ambiguous name fails the batch and rolls it back. Features must follow their dependencies in history. Removal of a dependency without also removing/replacing its dependants fails atomically.

## Tools

| Tool | Purpose |
| --- | --- |
| `doc_info`, `doc_get` | Compact summary, or `detail:design` for names, values, volumes and bounds without expressions or meshes / full parametric document |
| `doc_apply` | Atomic modeling batch |
| `doc_undo`, `doc_redo` | Persistent iteration undo/redo after reopen; revision increases |
| `history_info`, `iteration_restore` | Bounded iteration metadata and branch-preserving authoring restoration |
| `history_migrate_derived` | 049A explicit, confirmed journal rewrite removing derived baked frames after a verified backup |
| `hud_info`, `hud_flex_check` | Read-only Parametric HUD inventory with dependents, and a sampled min/mid/max scratch rebuild check (topic `hud`) |
| `eval_expr` | Finite arithmetic and trig evaluation |
| `mesh_inspect` | Indexed topology, measurements and explicit validation limits |
| `mesh_raycast` | Nearest triangle hit, normalized direction, distance in mm |
| `mesh_get` | Positions and triangle indices, explicitly requested |
| `doc_save`, `doc_load` | Native document persistence |
| `export_mesh` | STL/OBJ of terminal outputs, a selected feature ID, or a unique instance name plus `copy` |
| `snapshot` | Software-rendered PNG of terminal outputs, a selected ID, or a unique instance name plus `copy`. Z-up `iso`, `front` (-Y), `right` (+X), `top` (+Z), or an explicit [custom camera](SNAPSHOT_CAMERA.md). The image is a display mesh, not STEP authority |

`doc_info` filters `ids` and `sections` require `detail:"design"` or omitted
`detail` (which infers design). Explicit `detail:"summary"` accepts neither
filter. Incompatible detail returns `INVALID_ARGUMENT`, stage `arguments`,
and corrected `recovery.retry_arguments` retaining the exact requested filters.
This read cannot modify the document; revision conflicts remain distinct.

The process has local filesystem permissions of the host. Save/export tools write the supplied paths. File saves replace existing documents; there is no cross-process file locking. MCP sessions are local, not a remote HTTP service.

## Numerical contract

Document lengths use millimetres. `sin`, `cos`, `tan`, `asin`, `acos`, `atan2` operate in radians; `deg(x)` converts degrees to radians. Transforms' `rx/ry/rz` and revolve's `angle` use degrees. Expressions support `+ - * / ^`, parentheses, scientific notation, `sqrt`, `abs`, `min`, `max`, `pow`, `floor`, `ceil`, `pi` and `e`. Use explicit parentheses around negative bases when exponentiating.

Surface coordinates use `u` and `v`; defaults are domains `[0,2*pi]`, with `nu=96`, `nv=48` subdivisions. Resolution is limited to 3..256 per axis. `wrap_u=1` and `wrap_v=1` request shared seam indices; sampled endpoints must agree within 1e-6 mm. This is a sampled seam check, not a symbolic continuity proof. Mesh buffers use f32, while expressions and analytic geometry use f64.

Reports use indexed connectivity without silently welding/healing. `closed_oriented_edges=true` only means the implemented edge, degeneracy and finite-data checks passed. `solid_certified=false` and `self_intersections_checked=false` remain explicit. Signed volume is provided only for meshes with closed oriented edges and is not a certified physical volume. Triangle IDs belong to one tessellation; do not retain them across edits.

## Capacity and performance

Documents are limited to 4 MiB on load, 1024 features, 256 parameters and 256 sketches total across all parts and assemblies. Assembly nesting does not create a separate sketch allowance. A sketch accepts 128 entities and 256 constraints. JSON nesting is limited to 64; mathematical expressions to 2048 characters. These are request limits, not a bound on all possible boolean or solver runtime. There is no cancellation or worker isolation yet.

Mesh buffers, reports and BVHs are cached per session revision and selected feature ID. One geometry recompute handles the whole batch. Version-27 documents retain immutable iteration deltas and shared large source strings, with persistent undo/redo. Limits are 512 nodes, 131072 replay operations and the total 4 MiB document cap; excess capacity rejects the mutation atomically. Existing documents acquire only their first observed baseline. Evaluated meshes and usage snapshots are excluded from the journal. See [persistent iterations](ITERATION_HISTORY.md). The old hand-written SIMD routines are retained as source archaeology but are disabled until ABI/numerical parity and profiling justify them.

## 0.3 additions

`assembly_info`, `tube_inspect`, `compute_info` and `compute_benchmark` bring the tool count to 17. `doc_apply` accepts `assembly.add`, `assembly.edit`, `assembly.remove`, and tube features with a top-level `bends` array. Mesh tools accept `node` for world-space instances or `id` for local part definitions, never both. Full contracts and examples are in [MECHANICAL.md](MECHANICAL.md). Version-2 `.ark` documents preserve the assembly hierarchy and bend programs. The compute backend is optional, with explicit capability reporting and CPU fallback.

## 0.4 preview operation

`notch_plan` was the 18th tool in 0.4. It returns a standalone `ark-cut-plan` for an ideal zero-radius retained-wall notch, with cutter geometry and mathematical checks. It does not edit the active document or claim source reconstruction/physical validation. The current C++ `step_inspect{path,faces?}` tool provides a read-only OCCT inventory of STEP sources; the former Python audit script has been superseded.

## 0.5 integrated workspace

`notched_tube` is now an editable feature with stock expressions and a replaceable `notches` array. `notch_inspect{id,progress?}` reports the plan and optional display transforms. `export_step{id,path}` writes the straight part using the configured OCCT backend and reports its validation. These additions bring the tool count to 20. Animation is read-only and cannot change the exported pose. See [BEND_WORKSPACE.md](BEND_WORKSPACE.md).

## 0.6 source-preserving import

Use `import_step{path,replace?}` for the bounded planar finite-hinge source domain. `source_inspect{id,progress?}` provides the measured projection/holes without the large payload of `doc_get`. `export_step` now handles `source_tube` as well as designed `notched_tube`. The source geometry is read-only and the imported package is version 4. These additions bring the tool count to 22. See [SOURCE_STEP.md](SOURCE_STEP.md) for validation and failure boundaries.


## 0.7 parametric sketches, onboarding and live Studio

`agent_onboard{topic}` is the first tool to call. Topics `overview`, `sketch`,
`tube` and `assembly` provide compact current-state context, supported operations,
validation boundaries and relevant examples. The sketch example includes the
current `expected_revision` and can run on a fresh document.
`doc_new{expected_revision}` starts an empty document with undo and a monotonic
revision; it never resets revision to zero or overwrites saved files.

`plane_inspect{name}` and `sketch_inspect{name,samples?}` bring discovery to
**26 tools**. `doc_apply` adds `plane.set`, `plane.remove`, `sketch.remove`,
`sketch.entity.set`, and `sketch_cut` features. Construction planes, driven
coordinate/dimension expressions and interpolating splines are specified in
[PARAMETRIC_SKETCH.md](PARAMETRIC_SKETCH.md). Inspections do not mutate revision.

A standalone `ark-mcp` still owns its own document. `ark-mcp --connect <endpoint>`
uses the live Studio bridge; all tool calls then execute on Studio's owning
thread and share its document, revision and undo history. Disconnect never
falls back to an unrelated empty session. [Codex configuration](CODEX_CONNECT.md).

The in-app assistant uses a configured Responses-compatible provider and these
same tools. [Connector contract and testing](ASSISTANT.md). Onboarding and
geometry operations also work without an API key or an LLM.

## 0.8 native OCCT modeling and sheet sources

The 0.8 API had **30 tools**. `agent_onboard` added `surfacing` and `sheet`
topics to its earlier overview/sketch/tube/assembly topics. `kernel_info{}` reports the native OCCT
version and capability status. `step_inspect{path,faces?}` inventories an
original STEP file without editing the document. `native_inspect{id}` returns
compact native B-rep validation and opening evidence without transmitting
large mesh or B-rep buffers. `import_sheet_step{path,k_factor,body_index?,
root_panel?,replace?}` imports a supported source sheet with an explicit
K factor; a multiple-body file requires a zero-based body index, and equal
flanges can require root panel 0 or 1.

`doc_apply` supports authored `loft`, `surface_fill`, `bspline_surface`,
`thicken`, and `sheet_metal` features. Use `native` for loft sketch names,
NURBS poles/weights, advanced fill profiles, and sheet openings; use `p` for
editable expressions. Source sheets are read-only. `export_step{id,path,pose?}`
exports exact native B-reps; sheet pose is `flat` by default or `formed`.
The Formed/Flat Studio display choice does not edit the document. See
[surfacing](SURFACING.md), [sheet metal](SHEET_METAL.md), and
[native Studio controls](NATIVE_STUDIO.md).

The previous custom primitive and mesh features remain available. Native
surfaces and sheets cannot be passed through approximate mesh transform,
Boolean, or sketch-cut operations. Use authored parameters, construction
planes, or assembly instance placements. The native worker and current tests
are C++; no Python runtime or test gate is required. The Windows native build
uses OCCT 8.0.1 from [the pinned SDK](NATIVE_BUILD.md).

## 0.9 exact references, assembly mates and authored sheet graphs

The API currently lists **34 tools**. `reference_inspect{id,reference?,fraction?}`
discovers producer-authored exact face, edge and axis names and resolves their
local frames. `sketch_diagnose{name}` or `sketch_diagnose{sketch}` reports
per-constraint residuals and local rank without changing the document. Use
these names in supported construction-plane supports and assembly mates;
triangle and B-rep enumeration indexes are not persistent references.

`doc_apply` accepts `assembly.edit{id,fixed:true}` and
`mate.add{a,b,type,reference_a,reference_b,value?,fraction_a?,fraction_b?}`,
`mate.edit{id,...}`, `mate.remove{id}`. The two IDs identify part instances.
They may sit under different groups. A mate across groups moves each immediate
parent and its descendants as one rigid body. A same-parent mate still moves
only the part. Every connected mate set needs a fixed anchor: a fixed endpoint,
or, across groups, a parent group that contains a fixed descendant.
Types are `concentric`, `coincident`, `contact`, signed `distance` in mm,
and `angle` in degrees. Solved instance transforms are derived after feature
recompute; authored placement and value expressions remain in the document.
`assembly_info.diagnostics` reports mate residuals, local rank, remaining
degrees of freedom and unconstrained instances. Invalid or stale references,
unanchored systems and unsatisfied mates fail the full `doc_apply` batch.
See [the mate contract](MECHANICAL.md#grounded-mates).

The version-7 `.ark` package retains fixed instance flags, mate definitions,
native solid programs, supported construction references and sheet authoring
programs. Older document versions remain readable. `native_solid` performs
supported exact primitive and Boolean operations through the C++ OCCT worker.
The newer sheet program keeps a base panel, ordered flange attachments and
panel-owned openings; its formed/flat B-reps and refold checks remain geometric
evidence, with bend trajectory collision and material calibration outside
current validation. `sheet_preview{id,progress}` returns read-only sequential
flat-to-formed panel transforms at progress 0..1 for display; it does not
edit the document, change the exact export pose, or certify a bend trajectory.

The 0.7 live Studio connection remains the way an MCP client works on the
currently open document. A standalone `ark-mcp` process still has its own
document, and session edits still require the current revision for safe batches.

`import_mesh{path,name?,weld_mm?,expected_revision?}` is the 34th tool. It
reads `.stl` or `.obj` once and stores a `mesh_body`. `weld_mm` is optional and
must lie in `(0, 1]`. The file is not linked. Caps are 12288 vertices, 4096
triangles, coordinates within 1e6 mm, a 32 MiB source file, and the existing
4 MiB document limit. OBJ faces with more than three corners are fanned from
the first vertex and are limited to 64 corners. Binary STL is recognized when
the file length is `84 + 50 * count`; otherwise the text form is read.

`doc_apply` also accepts exact `sweep`, `extrude_surface`, `revolve_surface`,
`offset_surface`, and `sew`, plus sampled `mesh_body`, `mesh_weld`,
`mesh_smooth`, `mesh_decimate`, `mesh_offset`, and `mesh_fill`. Exact surface
arguments and examples are in [surfacing](SURFACING.md). A document that
contains one of these features is version 8. Save chooses the document
version. Draft, `linear_pattern`, `circular_pattern`, `mirror`, fillet or
chamfer `native.selection`, loft smoothing or compatibility, or a one-point
loft tip makes that save version 9. Mirror accepts `native.plane` as `XY`,
`XZ`, `YZ`, or a datum name, or `native.face` for one planar face. Selecting
a face, edge, or curve in Studio stores that semantic name and does not add
a further version. Older documents stay at their previous version and remain
readable. The product version stays 0.9.0 and the tool list stays 34.

`assembly.add` and `assembly.edit` accept `pattern` with `type` `linear` or `circular`, an integer `count` from 2 to 32, and `dx`/`dy`/`dz` or `step`/`axis` (`X`, `Y` or `Z`). One node owns the copies. A mate solves that node once and the copies follow. Degrees of freedom count the node once; the BOM counts every copy. `native_solid` `union`, `cut` and `common` accept 2..16 feature inputs and/or `native.instances` naming placed parts, including every pattern copy. Feature inputs stay in part coordinates. Instances are transformed by the solved assembly pose and then booleaned into one exact solid. A round tube is an exact annular sweep along its centerline, initially +X, and can enter a boolean or a concentric mate. A rectangular tube is an exact hollow sweep along the same centerline, initially +X, with width along the section normal and height along the binormal, and can enter a boolean. The display mesh remains the tessellation. Two-input booleans stay at the previous document version. Instance patterns, `native.instances`, or a union of more than two exact solids save as document version 10. `native.op` `shell` and a loft with `native.guides` save as document version 11. `assembly_info` returns `copy` on each instance. Its optional `candidate` probes a mate, including residuals and the two frames, without saving it or changing the revision. `interference` compares display meshes and stays the cheap screen; the default triangle budget is 8000 and `interference:true` raises it to 120000. `clearance` is the exact separation between two B-reps, with negative legacy values representing penetration estimates, and `measure` is a read-only distance and angle between two semantic frames. Neither writes the document. `feature.edit`, `feature.remove`, `assembly.edit` and `assembly.remove` accept a unique name; `assembly.edit` `parent` does too, and parent 0 stays the root. `mate.edit` and `mate.remove` keep numeric mate ids, or omit `id` and select the unique mate by endpoints `a` and `b`. `snapshot` and the mesh tools accept that node name plus `copy`. A smooth loft publishes `side.<entity>` with status `merged` when OCCT fuses the authored sides.

`doc_info` accepts `detail` `summary` or `design`. Design returns parameter values, feature volume and bounds, sketch entity kinds, and assembly copy counts, without expressions, native payloads or meshes. A revision conflict says to read `doc_info` with detail design. `sketch.patch` changes one sketch coordinate. `sketch_inspect` with `entity` returns that entity's length, bbox and tangents. `reference_inspect` accepts a sketch and resolves `point.<name>`, or `find` returns the unique planar face for a normal. `native_inspect` `point` returns a signed distance, negative inside one solid. A point datum, `on_curve` or `on_face` support, a sketch arc or wire, a tangent constraint, and `curve`, `intersection_curve` or `bridge` save as document version 12. Loft guides and sweep paths accept a curve feature name. A bridge of continuity 1 uses an end derivative equal to the unit tangent times the chord length. Continuity 2 matches those tangents and uses zero end curvature. The tool count stays 34 and the product version stays 0.9.0. `feature.edit` accepts `suppressed`. A true value leaves that feature out of recomputation and keeps its history and parameters. Sketch entities also include `project` `{feature,reference}` and `offset` `{source,distance}`. `feature` is an id or a unique name, and the saved document stores the id. `distance` is signed millimetres, positive to the left of the directed curve. Both entities are driven, and `sketch.patch` rejects them. `canvas.set` `{name,path,width,height,plane?,opacity?}` and `canvas.remove` `{name}` store a photo rectangle on a plane. The kernel does not decode the image. A plane canvas, a suppressed feature, or a projected or offset sketch entity saves as document version 13. The tool count stays 34 and the product version stays 0.9.0. `doc_apply` accepts `sketch.fillet` `{name,entity,corner,radius}` and `sketch.chamfer` `{name,entity,corner,distance}` on one wire. The corner is a vertex index. The radius or distance stays an expression, and the sharp wire remains the authority, so `sketch.patch` retrims it. A fillet is the minor arc on the turn side. Replacing the same entity and corner updates the size and the kind. A mate may join part instances in different groups. Across groups, the solve moves each immediate parent and its descendants as one rigid body about that group's authored origin. A same-parent mate still moves only the part. Optional `p.density` is grams per cubic centimetre, positive, finite, and at most 100. It does not change the document version. `native_inspect` then adds local `mass_g` and `center_mm`. `assembly_info` adds the assembly mass and the world center when every visible placed part has a density, and sets `mass_checked` false with `mass_reason` when one is omitted. A sketch fillet, a sketch chamfer, or a mate across groups saves as document version 14. The tool count stays 34 and the product version stays 0.9.0.

Analytic cylinder, cone and torus meshes run along +Y. A `native_solid` cylinder, cone or torus uses local +Z. Promoting an analytic cylinder, cone or torus applies Rx(-90 degrees), so the exact B-rep axis becomes +Z. Mate references `Z`, `origin` and `XY` use the instance frame +Z. Semantic reference `axis` is the exact B-rep cylinder axis. The analytic mesh axis is not retargeted. A round tube exact axis is its centerline and starts along +X. A rectangular tube exact axis is that same centerline: width follows the section normal and height follows the binormal. The tube display mesh uses that same centerline. Studio toolbar booleans stay refused while an assembly is active.

Mesh features store no B-rep. The kernel does not decode a photo. A prismatic
outline from a photo is a sketch wire followed by an extrude. A freeform photo
is a `mesh_body` whose `positions` are a row-major point grid and whose
`grid` is `{nu,nv}`, with `nu` and `nv` integers from 2 to 256 and `nu*nv`
points. The kernel builds the triangles. `grid` and `indices` together are
rejected. `mesh_body` keeps the authored `mesh:{positions,indices}` object
when the triangles are explicit; positions are three numbers per vertex and
indices are three per triangle, with the same vertex and triangle caps as
import. A grid document stores `grid` beside `positions` and remains version 8. Evaluation bakes f32 coordinates and writes a report onto the body.
`mesh_inspect{id}` attaches that report as `mesh_model` when the feature is a
mesh operation. `native_inspect` on a mesh feature fails and tells the caller
to use `mesh_inspect`.

`mesh_weld` takes one input. `p.epsilon` defaults to 1e-4 mm and must be in
`(0, 1]`. It may run on a non-manifold mesh. A result with no triangles is
rejected. `mesh_smooth` is a uniform Laplacian: `p.iterations` is an integer
from 1 to 64, `p.lambda` defaults to 0.5 in `(0, 1]`, and `p.pin_boundary`
defaults to 1. `mesh_decimate` collapses interior edges to their midpoint using
quadric error. `p.ratio` is required and lies in `[0.05, 1]`. Boundary edges
are not collapsed. If the input is closed and oriented, the output stays
closed; if it is open, the boundary-edge count stays the same. When the ratio
cannot be met without breaking that rule, the feature still succeeds and
`target_reached` is false. `mesh_offset` moves vertices along normals by
`p.distance`, with absolute value in `(1e-9, 1e4]`. A closed oriented input is
oriented outward first, so a positive distance grows it. The report's
`offset_model` is `vertex_normal`: corners of a box move by about
`distance/sqrt(3)` per axis, not by a constant shell. `mesh_fill` closes each
boundary loop that lies within 1e-4 mm of one plane, using an ear clip of 3 to
256 vertices. Non-planar loops fail and name the measured deviation;
`surface_fill` is the exact operation for a curved boundary.

Smooth, decimate, offset, and fill reject non-manifold, inconsistent,
degenerate, or non-finite input, and they accept at most 12000 triangles.
Every mesh report sets `mesh_authoritative` true, `exact_geometry` false, and
`self_intersections_checked` false. `sampled_from_brep` is true only after an
explicit `p.sample` of 1. Legacy analytic bodies have no B-rep and can enter a
mesh operation without that flag. Call `agent_onboard{topic:"mesh"}` for the
current example.

## 019B source and feedback contracts

Load `agent_onboard{topic:"skeleton"}` for native input/relation/vector-array and
pattern source. `skeleton_compile` is a read-only syntax/relations preview;
`skeleton_inspect` exposes ownership and input overrides. Use revision-bound
`doc_preview` and `doc_apply` with `skeleton.set` / `skeleton.remove` for geometry.
The responsible team lead owns global source changes. Specialists coordinate
application through that lead. [Source contract](SKELETON_AUTHORING.md).

`mesh_edit` retains an indexed selection recipe and the observed source topology
fingerprint, with mandatory `expected_revision`. Explicit sampling remains
necessary for exact inputs; mesh results keep triangle authority.
[Selection/closure limits](MESH_DIRECT_AUTHORING.md).

`collision_inspect` measures a pair of definitions or placed occurrences/copies.
Exact single solids and meshes expose separate authorities, checked properties
and unknowns. It is a read-only static geometry check, with no response dynamics.
[Native helix, loft, sewing and collision limits](PARAMETRIC_GEOMETRY_019B.md).
All three inspection/compile tools are available in read-only query batches and
postconditions. Image attachments include actual reference pixels, intent and
optional rough dimensions; use existing editable authoring tools and disclose
hidden-depth assumptions. [Image transport and authoring](IMAGE_AUTHORING.md).
