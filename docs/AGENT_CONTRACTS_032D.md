# Compact agent contracts — 032D

This work addresses **D.02** (agent contracts and coordination) and **A.04**
(observable evaluation and measured performance). The modes below reduce actual
serialized response bytes on the same synthetic document. Byte counts do not
establish token, inference-time, geometry-quality or manufacturing improvements.
Default responses remain compatible with existing clients.

| Request | Compact contract | Full evidence on demand |
| --- | --- | --- |
| `doc_info{detail:"revision"}` | Current revision, feature count, units, evaluation counters, parameter error, history tip and saved-geometry transport status when present. `response_scope:"revision_status"`. No object catalog or geometry evaluation. | Default/`summary` retains the legacy inventory. `design` supports `ids` and `sections`. Filters with `revision` reject. |
| `agent_onboard{topic,detail:"guide"}` | Complete topic guidance without `document`. `document_state_included:false`; guidance is static documentation, not current geometry evidence. Embedded onboarding packs use this mode. | Default/`full` also includes document inventory. |
| `doc_preview{...,detail:"changes"}` | Changed-feature partial evidence and compact candidate status. Created feature/node/mate IDs, sheet edits, measured postconditions, validation/errors, recipe, candidate/apply receipt and refresh guards remain available. | Default/`design` retains before/after design inventories. |
| `doc_apply{...,detail:"changes"}` | Revision/evaluation/created-ID/sheet-edit/check receipts without an inventory. The same atomic validation and publication run. | Default/`full`, or `design`, preserves the legacy response. Prepared apply continues to publish its retained receipt without another geometry evaluation. |
| `native_inspect{id,detail:"properties"}` | Current native validity, counts, volume, area, bounds and geometry authority flags, plus mass status/reason and measured mass when supported. No topology, opening inventory or operation diagnostics. | `report` retains operation diagnostics, including operation-specific options; `full` additionally includes topology/panels/bends. |
| `assembly_info{detail:"result",measure|clearance|drive|sweep}` | Exactly one requested operation, its full measured result and revision; assembly resolution diagnostics where applicable. No duplicate document catalog, BOM, aggregate mass or display-interference wrapper. | Default/`full` preserves comprehensive inspection. Multiple operations or unrelated result-mode options reject. |

Native properties remain exact B-rep evidence. `display_mesh_authoritative`,
`self_intersections_checked` and `manufacturing_authoritative` retain their
reported values. Omitting operation diagnostics does not upgrade their scope.
Assembly measurement retains both frames and the axis convention. Clearance
retains its exact checked/reason flags; sampled sweeps remain sampled evidence.
Saved-geometry transport status is not a declaration of completed native
validation. Use the relevant operation report for its remaining limitations.

Agents should load a guide once, read `doc_info{detail:"revision"}` for current
status, and obtain selected authoring/dependency context with `object_context`
before editing. Use `native_inspect{detail:"properties"}` for validity/properties
and `report` for operation-specific evidence. A compact receipt never permits
guessing IDs, omitting required checks or bypassing `expected_revision`.

## Read batches

`query_batch{queries:[{tool,args,fields?},...]}` accepts 1–128 queries. The
discovered `tool` enum is the executable allowlist:

`object_context`, `sketch_repair`, `feature_history`,
`assembly_authoring_info`, `holes_catalog`, `cad_inspect`, `drawing_inspect`,
`collision_inspect`, `skeleton_inspect`, `skeleton_compile`,
`reference_navigate`, `library_get`, `library_search`, `library_info`,
`library_sources`, `activity_info`, `bom_inspect`, `fastener_recipe`,
`manufacturing_inspect`, `physics_inspect`, `materials_inspect`, `wood_inspect`,
`doc_info`, `eval_expr`, `plane_inspect`, `sketch_inspect`, `sketch_diagnose`,
`reference_inspect`, `native_inspect`, `assembly_info`, `assembly_motion`,
`geometry_query`, `math_eval`, `mesh_inspect`, `frame_inspect`,
`electronics_inspect`, `sheet_inspect`, `suspension_inspect`, `tube_inspect`,
and `agent_onboard`.

Batched onboarding requires explicit `detail:"guide"`. `object_context`
retains its existing maximum of 64 selected objects, depth 0–2, per-row bounds
and 256 KiB result-body bound, with explicit truncation/omission metadata.
Neither tool can dispatch another batch. Mutation, export, snapshot and
recursive batch calls are absent from this allowlist.

`fields` means **top-level names inside each tool's `structuredContent`**,
not a dot path. It accepts 1–32 distinct nonempty names. For example:

```json
{"queries":[
  {"tool":"doc_info","args":{"detail":"revision"},"fields":["revision","feature_count"]},
  {"tool":"native_inspect","args":{"id":1,"detail":"properties"},"fields":["report","mass_checked","geometry_authority"]},
  {"tool":"object_context","args":{"ids":[1],"limit":1},"fields":["objects","revision","partial","evidence_limits"]}
]}
```

`report.volume_mm3` is a postcondition query path; it is not a valid top-level
batch projection name. Select `report` and read its property. Conditional
fields, such as mass values without a density, are not synthesized. An absent
requested field rejects with the name and the actual available fields. Unknown
query keys, invalid `args`, duplicate projection names, disallowed tools and
full batched onboarding reject explicitly. The query structures are checked
before dispatch. Projection updates text and structured data together.

The outer successful result contains `results` and `revision`. Every result
row retains the normal `structuredContent`, `content`, `isError` envelope and
adds `elapsed_ms`. An individual inspection failure retains its error envelope
and does not invent a projected value. Reads do not publish a new document
revision. Existing read-side activity logging and native result preparation
remain subject to their existing contracts.

## Cooperative write windows

Focused remains the default. Parallel roles should reason, inspect selected
objects and prepare concrete commands **before** reserving. Immediately before
the short preview/commit sequence, read the current revision and optionally call
`team_reserve_write{expected_revision,lease_ms?,reason?}`. The default lease is
10,000 ms, with an explicit 100–30,000 ms domain.

The grant includes the measured revision verification, `lease_ms`,
`lease_remaining_ms`, `auto_renew:false` and `lease_renewed:false`. The internal
verification requests revision status and retains activity/feedback custody
without a second feature inventory. Repeating a reserve for the same active
owned window returns its existing ID and remaining lifetime; it does not extend
the lease. A different revision requires releasing that window first.

Release promptly with the issued `reservation_id`. Cleanup of an expired or
already released own receipt is idempotent and reports `already_ended` plus
`reservation_status`. This only acknowledges cleanup: it grants no window,
changes no revision, clears no unrelated failures and cannot release a newer
reservation. Unknown or foreign tokens reject. Only the most recent 64 retired
receipts are retained; project boundaries and team cancellation/restart invalidate
them. Task/goal finish and owner-turn exit continue to release active windows.

Peers can read during a reservation; conflicting writes remain blocked. After
expiry or a peer commit, inspect the current revision and explicitly refresh or
re-preview when needed. Reservations never rebase, renew silently, steal another
role's window, alter discipline permissions or bypass atomicity/steering guards.

## Verification scope

`agent_contracts_032d_tests` compares actual serialized bytes for identical
public synthetic documents and checks equivalent status/guidance, compact
candidate postconditions, atomic rejection, stale apply, bounded read batches
and exact assembly measurement. Its native configuration also compares exact
box properties and authority flags. Team regressions exercise unchanged TTL,
expired-owner cleanup, foreign/unknown token rejection, preservation of a newer
window, project-boundary invalidation and compact verification receipts.

Runtime results and measured byte counts belong in the iteration verification
record after the registered tests run. These contracts do not promise a fixed
full-document rebuild latency or continuous motion/clearance qualification.
