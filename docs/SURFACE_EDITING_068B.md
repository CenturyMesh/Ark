# Surface group editing and exact face sets — 068B

**A.02 / B.01 / B.03 / D.02 — Verificado no escopo local.** See [068B verification](iterations/068B.md); broader milestones remain **Parcial**.

## Groups of exact control poles

`surface_control{action:group_plan}` extends the existing source-hash/revision-bound control-net contract. `selection` chooses explicit `poles:[[u,v],...]`, one `row` (`u`), one `column` (`v`) or a `rectangle` (`u_min/u_max/v_min/v_max`). `delta_mm` and `max_displacement_mm` are explicit. `influence_radius` is 0–32 **grid-index steps**, using smoothstep decay to zero; it is not a physical distance. `lock_boundaries` preserves named U/V boundary poles. A resulting edit must contain 1–128 nonzero displacements; oversized selections reject without truncation.

Optional `symmetry:{direction:u|v,axis:x|y|z,origin_mm}` mirrors each selected pole to the reversed U or V index. The whole original control net must already be spatially mirrored within 1e-6 mm, paired rational weights must be equal, and knot multiplicities/reflected normalized knot positions must match (roundoff allowance 3e-14). Conflicting or locked partners reject. This maintains an explicitly declared existing symmetry; it does not guess a symmetry plane or remesh the surface.

Plans expand to the existing `deform_poles` recipe and preserve degrees, knots, multiplicities and weights. Native validity, interference, sampled Jacobian and displacement guards remain in force. No new document semantic data is needed for group selection because the retained output is ordinary explicit pole edits.

Studio **Group edit** (49506) exposes selection, displacement, influence, locks and mirror fields. Ctrl-click toggles poles; dragging a selected group moves all selected poles through the existing coalesced exact preview. Group-dialog previews replace the staged cage explicitly. Apply, Reset, Cancel, source revision/epoch invalidation and one Undo keep their existing behavior.

## Trim selected faces as one operation

`multi_trim_plan` accepts 1–16 source definitions (`sources:[id|{id,face}]`), one `tool_id`, and a definition-local `keep_points` XYZ for every source. A face in a multi-face or solid definition needs an exact semantic reference. To select several faces of the same producer, explicitly extract those faces first. At least one face must actually split; untouched selected faces remain. Every split region is measured and listed, and the keep point must select one interior region unambiguously. The result contains exactly one retained face per explicit source and is not silently sewn.

The persisted SurfaceEdit operation is `multi_trim`; inputs hold sources followed by the cutter, which stays a separate output. Neither cutter extension nor tolerance enlargement is automatic. New operation semantics require version 57.

## Extend a rectangular support while keeping holes

`operations_inspect` returns exact source references and, for one open face, native UV bounds and hole count. `extend_trimmed_plan` accepts either explicit expanded `bounds` or existing bounded-support `extension:{direction,length_mm,continuity}`. The outer wire must be an exact rectangular UV boundary. Original inner wires and their native pcurves are retained, with native validity, positive added area, hole count and sampled original-support checks.

Arbitrary outer trimmed contours, solids and multiple faces reject. This improves the supported rectangular-with-holes domain; it does not claim general trim-preserving NURBS extrapolation or global continuity certification. UV coordinates are native support parameters, not generally millimetres. Human-entered keep-point, UV-bound and extension-length expressions remain authored expressions after validation. The persisted operation is `extend_trimmed` and requires semantics 57.

## Diagnose and sew explicit surface sets

`sew_inspect` evaluates 2–16 exact face/shell definitions privately at the unchanged native sewing tolerance. It reports connected components, free boundaries, exact lengths/endpoints, source semantic-edge matches when topologically traceable, and nearest separated-boundary distances. A nearest edge is a diagnostic, not an authorized repair pairing. No face is dropped and no gap is healed by increasing tolerance.

`sew_plan` prepares the existing Sew feature, with explicit `solid` and `require_connected`. Failure to close a solid rejects the candidate. Studio provides separate diagnostic and candidate-review actions; the actual evaluated candidate image is shown before Apply. Cancelling leaves the source design intact.

## Verification scope

`surface_operations_068b_tests.cpp` independently checks group falloff/locks/mirroring, basis preservation, native preview/persistence, analytic multi-face area conservation, ambiguous keep-point rejection, exact open-edge diagnostics, invalid solid sewing and analytic extended hole area. The existing surface-control UI test gains real group fields, four-pole preview, Apply and Undo; `surface_operations_selftest_068b.inl` exercises actual trim/sew controls and captures. No private drawing is a fixture.
