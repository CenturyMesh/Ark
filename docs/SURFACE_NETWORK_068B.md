# Curve networks and loft correspondence — 068B

**B.03 / B.01 / A.02 / D.02 — Verificado no escopo local.** See [068B verification](iterations/068B.md); broader milestones remain **Parcial**.

## Exact polynomial curve networks

`surface_control` action `network_plan` takes explicit `u_curves` and `v_curves` arrays, each containing 2–9 native curve feature IDs or unique names, and `expected_revision`. Optional `name` labels the result. Inputs are definitions in one shared local frame; occurrence placement is not inferred.

Each curve must contain one open, non-rational polynomial edge of degree 1–8: a line, Bezier, or equivalent clamped single-span B-spline. Unsupported rational, multispan, closed, singular or inconsistent networks reject. No input curve is silently fitted, reconstructed from a mesh, or discarded.

`u_stations` specifies where the V-family curves cross each U curve; `v_stations` specifies the U-family positions on each V curve. Both are ordered normalized parameters, start at 0 and end at 1, and default to uniform spacing. Optional `u_reverse`/`v_reverse` boolean arrays explicitly reverse individual curves before checking crossings. Normalization refers to the exact polynomial parameter, not arc length.

The worker exactly elevates compatible polynomial bases to a shared Bernstein basis (at least cubic in both directions, yielding at least 4×4 controls for subsequent matching/editing) and constructs the Gordon Boolean sum:

`S(u,v) = sum_j L_j(v) C_j(u) + sum_i L_i(u) D_i(v) - sum_ij L_i(u)L_j(v) P_ij`.

The retained `surface_edit` recipe uses `native.op:curve_network`, the ordered curve inputs, `u_count`, and optional station/reversal arrays. It recomputes from those editable native source curves. It does not retain copied B-reps. New recipes require conditional document semantics 57.

All intersections are checked against the fixed 1e-6 mm position tolerance. The result is one native polynomial face stored in an exactly equivalent clamped B-spline basis for subsequent control-net editing, checked for B-rep validity and native self-interference. Independent 65-point checks on every source curve report measured preservation error; a 17×17 derivative grid rejects observed singular Jacobians. `global_injectivity_verified:false` remains explicit: samples do not prove all between-sample behavior. `curve_basis_identity` is true only when native crossing points coincide numerically without residual gap.

The action returns a retained, validated `doc_preview` candidate and `network_evidence`. It does not publish. Apply the candidate at the observed revision; stale publication rejects.

Studio **Surface from curve network** (49980) offers ordered source lists, optional station values and explicit reversal lists. It shows the actual evaluated candidate image and measured crossing/curve error before Apply or Cancel.

## Loft correspondence diagnostics

`surface_control` action `loft_diagnose` takes an existing loft `id`, or a proposed ordered `sections` list, with optional `guides` and `controls`, plus `expected_revision`. It reuses the normal native sketch/curve/datum profile resolution and section alignment. It returns topology, endpoint and 32/33-point arc-length correspondence measurements without publishing or silently choosing section direction.

The report compares direct and reversed correspondence RMS distances. A materially shorter reversal is a review suggestion, not proof that the chosen loft twists or an instruction to reverse it automatically. Closed/open topology changes and different edge counts remain visible. The final loft still passes its existing geometry and guide-deviation checks; diagnostics do not relax those tolerances.

Studio **Loft section correspondence** replaces the previous alignment JSON input with typed per-section direction, seam edge and circle seam angle controls. It preserves settings by section name when explicitly reordering, displays measured preflight evidence, then previews the complete loft before publishing. **Loft diagnostics** (49981) opens the read-only report for the selected loft.

## Verification scope

Native tests compare a six-curve network against an independently defined biquadratic analytic surface throughout a 13×11 grid, exercise exact curve reversal, inconsistent crossings, station/tolerance rejection and reversed loft diagnostics. Host tests cover candidate isolation, revision rejection, editable source dependencies and save/reopen/recompute. Studio tests exercise real forms, candidate cancellation, publication, typed alignment, diagnostics and persistence. All fixtures are synthetic; no manufacturing or global continuity qualification is implied.
