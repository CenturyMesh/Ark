# 059C surface authoring forms

Milestones **A.02 / B.01 / B.03 / D.02**. Status: **Em curso** for integrated
Studio verification. The forms call the exact, atomic document API and capture
both document revision and document epoch before the first interaction.

## Operations and integration

| Action | Form | Authored contract |
| --- | --- | --- |
| 49400 | Mirror and join | `surface_edit`, one input, `native.op:mirror`, named plane or explicit origin/normal, G0/G1 |
| 49401 | Trim by tool | `surface_edit`, source and tool inputs, `native.op:trim_tool`, explicit surface/solid/curve tool and local keep point |
| 49402 | Ruled surface | `surface_edit`, two exact curve inputs, `native.op:ruled`, explicit second-curve reversal |
| 49403 | Loft | Ordered sketch/3D-curve/datum-point names, optional sketch/curve guides, existing loft controls and sampled guide checks |
| 49404 | Supported patch | Closed sketch or exact curve boundary, optional interior curve guides, individual G1/G2 boundary support forms |

Include `surface_workflow_059c.inl` after the existing `surfaceEditDialog`.
Dispatch creation through `surfaceWorkflowAction059(id)` and intercept feature
editing with `surfaceWorkflowEdit059(feature)` before the previous type switch.
Both return whether they handled the action. IDs outside 49400–49404 remain
available to other modules.

Include `surface_workflow_selftest_059c.inl` with the other Studio selftests and
route `--self-test-surface-workflow-ui` to
`surfaceWorkflowSelfTest059(hdc, shotPath)` in the initialized headless GL window.
The implementation uses the shared native editor, scroll container, accessible
field labels, keyboard handling and semantic selection controls.

## Editing and exact authority

Every form retains a copy of the original feature. Publishing updates name,
parameters, native recipe and the complete explicit input list together. Removed
optional parameters use `p_remove`; this is necessary because `feature.edit.p`
merges values. Loft section alignment and unchanged checkbox expressions survive
editing. New, duplicate, self or forward references are validated before publish
where appropriate, then the host and native worker validate the complete result.

The mirror form reflects and joins a valid half-surface at its boundary seam;
it does not smooth the source to force G1. Trim retains the exact region selected
by the local keep point and relies on the native report for discarded regions.
Ruled rails remain explicit; reversing a rail is a user decision.

Loft sections use names separated by semicolons. Native curve dependencies are
included explicitly. Point tips and guide compatibility remain subject to the
existing kernel contract. Optional guide samples are 2–129 per edge and the
sampled deviation limit remains within 1e-6–0.05 mm. Existing alignment recipes
are preserved; changing the section count while retaining incompatible alignment
is rejected rather than guessed.

Patch boundaries use `p.sketch` or `native.boundary_curve`. Interior guide IDs
are stored as `native.guide_curves` and included in inputs; at most 16 distinct
guides are accepted. They constrain G0 interior geometry. Supported edges use
the existing `native.constraints` contract with an edge index, support feature,
current semantic face and continuity 1 or 2. Unconstrained edges remain G0.
The UI checks a unique bound reference in the support's current digest-bound
producer inventory; the host resolves the exact B-rep and topology. The user
does not supply B-rep data or a fabricated topology digest. Blend requires at
least two distinct support faces. Cancelling any support form publishes nothing.

## Verification record

The isolated native Studio compilation succeeded. The first complete run reached
85 checks before its explicit interior-guide evidence assertion failed against
the earlier worker, whose output lacked `report.guide_curve_evidence`. This
failure is retained as evidence, and does **not** validate interior guides.
Final integrated verification is pending the guide-capable worker.

Observed passing scope in that run: all five main-form cancellations, mirror
area and source retargeting, invalid/self-source rejection, surface and curve
trims with analytic areas, ruled area, loft with exact 3D sections and a lateral
guide, retained section alignment, optional parameter removal, sketch-to-point
loft, cancellation at the second patch step, native G1/G2 support publication,
unknown support-face rejection and exact closed-wire boundary publication.

Native dialog screenshots were inspected. Labels and hints were shortened after
the first captures exposed clipping. Final captures, document round trip,
concurrent-publication/epoch rejection and combined guided G1 patch are exercised
by the same selftest and require the pending successful full run.

All fixtures are synthetic. Sampled continuity and guide errors are bounded local
checks, not proofs between samples, global continuity certification or production
qualification. No external CAD integration or manufacturing qualification is
claimed. No private document is part of the tests or delivery.
