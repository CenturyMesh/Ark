# Surface matching and analysis — 068B

**A.02 / B.01 / B.03 / D.02 — Verificado no escopo local.** See [068B verification](iterations/068B.md); broader milestones remain **Parcial**.

The existing `surface_control` tool has three additional actions. They require the observed `expected_revision`, an exact source feature and an optional semantic face reference. A multi-face source requires an explicit face.

- `quality` samples native D2 surface derivatives on a 3–65 square UV grid (default 17). It reports signed principal, mean and Gaussian curvature, positions and normals. Points outside trimmed regions and singular samples remain explicit. The grid does not certify extrema between samples. Nonrectangular or holed trims render classified sample points rather than interpolated cells that could bridge holes.
- `seam` samples explicit natural sides (`u_min`, `u_max`, `v_min`, `v_max`) of source and target faces, with optional reversed correspondence and 3–129 checkpoints. The report separates position gap, unoriented tangent-plane angle and the native second-fundamental-form tensor in a common tangent basis. Sides crossing outside a trimmed face reject. This is not arbitrary edge-chain matching.
- `match_plan` prepares one cached `doc_preview` candidate. It needs source/target B-rep hashes, explicit sides, continuity 0/1/2, a positive `max_displacement_mm`, and optional positive `cross_scale` (default 1). Review the candidate and apply its ID with the same revision.

Matching currently accepts open clamped untrimmed **non-rational BSpline** faces with compatible tangential degrees, pole counts and normalized knot vectors (including multiplicities). Reverse correspondence is explicit. No hidden resampling, tolerance increase or rational-weight approximation occurs. Incompatible inputs reject and leave the source unchanged.

G0 adjusts the boundary row; G1 also adjusts the first interior row; G2 also adjusts the second interior row. Source normal degree and row count must support the requested order while retaining the opposite boundary. Normalized parameter derivatives establish correspondence; `cross_scale` controls cross-boundary derivative magnitude. The exact source basis and target surface remain unchanged. Every moved pole respects the declared displacement bound.

The resulting native face passes B-rep validity/interference checks and a 17×17 regular-Jacobian check. Independent native D2 seam measurements verify the requested continuity at checkpoints: position ≤1e-6 mm, angle ≤0.001 degrees, curvature-tensor difference ≤0.0001/mm. `global_continuity_verified` remains false. The checks are not a proof between samples or manufacturing qualification.

The retained `surface_edit` recipe uses `native.op:match_boundary` with source and target as dependencies, current hashes and explicit references. The source is consumed as the edited output; the target remains a separate output. Changed dependencies require a new inspected plan. The recipe does not embed target B-rep bytes.

## Studio

**Surface quality** displays a native-sampled curvature map or camera-dependent zebra reflections. Blue means low absolute principal curvature; red means the report's maximum. Zebra stripes and interpolated triangles are display diagnostics; they never select production geometry. The report retains exact sample statistics and trim/singularity counts.

Pair forms use explicit readable ID-qualified feature dropdowns and optional semantic face fields. In an assembly, the source dropdown retains the occurrence selected before opening; choosing a target never changes that source frame. Ambiguous or different target frames reject.

**Check surface seam** displays the two sampled boundaries and gap connectors, alongside numeric evidence. **Match surface boundary** opens explicit source/target/side/continuity/displacement controls, displays the checked exact candidate in cyan, and requires a separate **Apply surface match**. **Hide surface analysis** cancels the visual review. Revision/document, source/target visibility or retained occurrence-frame changes invalidate overlays and reject stale Apply. Placed geometry requires a selected source and a unique target in the same authored occurrence frame; transient display poses reject. In an assembly, Match offers an explicit choice between **New occurrence in source frame** and **Definition only**. The new occurrence uses the candidate's actual feature ID and is included in the same reviewed atomic preview. Original occurrences are never silently retargeted; definition-only deliberately adds no placement.

Validation sources: `tests/surface_quality_068b_tests.cpp` checks analytic parabolic-cylinder curvature, independently expected G0/G1/G2 pole rows, exact tensor readback, source preservation, hash/bounds/tolerance rejection, target visibility and save/recompute. `surface_quality_selftest_068b.inl` exercises actual controls, all three display modes, matching preview/apply/undo and stale rejection on synthetic surfaces.
