# Scoped feedback review for 024B

Related milestones: A.04, B.03, D.02/D.03. Status: **Parcial**.

An explicitly authorized local project was read through `doc_load` and paginated
`activity_info`. The source was not edited, saved, committed or packaged. Only
aggregate observations and generic technical findings are retained here; raw
logs and the design remain local.

The ledger held 111 actions between 2026-10-14 13:42:18 and 14:04:53 UTC,
84 immutable feedback entries, 27 pending entries and 12 failed actions.
11 feedback entries contained an issue. Dedicated wished-tool and wished-option
fields were empty. Feedback is a subjective agent self-report, separate from
measured outcomes and host timings.

| Observation | Response and remaining boundary |
|---|---|
| Inline snapshot rejected while another role owned a write reservation | Classify snapshots without a path as document reads for permission and lease checks. Path exports keep write guards. Retain expected_revision checks. |
| Native inspection topology response too large | Add `detail:report`, preserving measured report fields and explicitly marking omitted topology. Default full response remains compatible. Measure payload on an equivalent synthetic solid. |
| Shell offset produced invalid B-rep | Keep explicit rejection. The agent's alternate cavity loft is not proof of general shell repair. Reproduction and robust exact domain work remain. |
| PCB/battery mesh query rejected degenerate triangles; exact compound query unsupported | Preserve geometry validity and scope boundaries. Do not silently drop triangles or infer exact clearance from a display proxy. |
| Viewport snapshot bounds invalid | Record failure; a software fallback is different evidence and cannot prove actual Studio rendering. Reproduction remains open. |
| Expired reservations and stale BOM/snapshot revisions during peer edits | Preserve concurrency guards. Coordinate final review after peer publication; do not renew leases or rebase writes silently. |

No design-cost, quality or broad performance reduction is inferred from these
records. A smaller synthetic response is a payload measurement, not a live-agent
token or cost benchmark. Failed attempts remain part of the evidence.

The equivalent synthetic box inspection returned 5,644 bytes in full mode and
1,485 bytes in report mode, with identical measured report fields. Local team
tests also verify inline snapshot access during a peer write reservation while
file exports remain guarded. See [the receipt](../verification-024B.json).
