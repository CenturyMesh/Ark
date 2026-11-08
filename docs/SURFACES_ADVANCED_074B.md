# Explicit surface construction in 074B

Milestones **A.02/A.04/B.01/B.02/B.03**. Native verification is local and
synthetic. Human-control verification and the final release gates are recorded
separately in the iteration receipt; this document is the construction contract.

## Boundary matching

The Match boundary form offers **Preserve basis** and **Exact compatibility**.
Existing recipes retain the former behavior. Exact compatibility persists
`native.basis_mode:"exact_compatible"` on the `surface_edit` / `match_boundary`
feature and keeps both source IDs as explicit dependencies.

The advanced mode accepts nonperiodic BSpline faces, including rational weights,
different degrees/knot vectors and rectangular iso-parametric trims. It segments
the actual face domain and refines the tangent basis by degree elevation and
knot insertion. Boundary rows are solved in homogeneous coordinates for the
requested G0/G1/G2 relationship. Weights must remain finite and positive, and
the authored displacement bound still applies. The opposite boundary and the
target remain fixed. Native validity, self-interference, position, normal and
curvature checkpoints remain required.

Arbitrary trims, inner wires and periodic matching are explicitly unsupported.
Constrained filling remains a separate construction for suitable nonrectangular
boundaries. Native checkpoints are evidence of the measured boundary behavior,
not a global G2 certificate between every checkpoint. The refinement domain is
bounded to degree 12 and 128 poles per surface direction.

## Curve networks and loft correspondence

The Curve network form's **Exact compatibility** option persists the same
`basis_mode` and exposes **Closed U / authored seam** and **Closed V / authored
seam**. It constructs a rational homogeneous Gordon BSpline from the existing
native curves. One finite native edge per curve can contain multiple spline
spans. Input families must have compatible rational gauges at all explicit
crossings; the result must have positive weights and pass native Jacobian,
curve-preservation and B-rep checks. Incompatible input data is rejected without
fitting replacement curves.

Closed directions require the corresponding explicit `u_closed` or `v_closed`
marker. Stations begin at 0 and end below 1; the final station at 1 reuses the
authored seam internally. No source curve ID is duplicated or silently rotated.
Stations address the normalized exact BSpline representation; they are not
arc-length or angle fractions. An exact analytic-to-rational conversion can
preserve the curve while changing its parameterization. Authored crossing
stations must match that representation; inconsistent crossings are rejected.
Closure means the measured positions coincide; it does not certify periodic
derivatives. Each family still contains 2–9 distinct inputs.

The Loft correspondence form can request **Review native seam proposals**.
Proposals compare native equal-arc-length samples against the preceding section,
keeping the first section anchored. Closed one-edge proposals use a 32-station
cyclic search followed by a native parameter solve. Multi-edge proposals use
existing edge starts. These are visible, editable suggestions, not a guarantee
of a globally optimal or twist-free loft.

`native.controls.section_alignment[].seam_parameter` is a normalized fraction
in `[0,1)` of the authored edge's native parameter interval. It is not an
arc-length fraction. Nonzero fractions require all non-point sections to be
single closed edges and `p.compatible:0`; they cannot mix with nonzero
`seam_edge` or `seam_angle`. The producer splits exact source curves on a shared
cyclic partition so all sections have matching edge topology. It checks the
original perimeter and disables OCCT's automatic correspondence rematching.
Split references receive explicit `.segment.N` names. Source curves remain
unchanged, and Apply publishes one checked candidate with one Undo.
The producer retains the actual edge returned by native wire construction.
Boundary references bind by native identity: complementary cyclic arcs can
share endpoint pairs and must still remain distinct edges. Ruled and nonruled
lofts both require all generated segment faces and boundary edges to stay bound
and distinct in the regression tests.

## Offset and thickness

The Offset and thickness check form exposes three construction methods:

| Form choice | Persistent `native.method` | Construction and scope |
|---|---|---|
| Simple | absent or `simple` | Previous explicit behavior |
| Joined intersections | `joined` | Native adjacent-face intersection offsets; open-shell thickening with generated side walls |
| Planar exact | `planar` | One exact planar face; translation for offset and prism for thickness, retaining inner wires |

Joined construction requires C1 surface supports. It disables surface
linearization and automatic self-intersection removal. It does not widen the
geometric tolerance, repair invalid input implicitly or discard extra solids.
Native failures retain their status and available fault bounds. Diagnostics use
the same constructor as the eventual candidate and expose normal-offset
checkpoints separately from sampled curvature-risk markers. Cancel does not
publish; a stale inspected revision cannot authorize Apply.

General-surface area integration requests OCCT adaptive quadrature at `1e-14`.
This is a measurement setting, not a geometric tolerance. A rational cylinder
fixture revealed that OCCT's reported integration error can be optimistic;
independent analytic area and native radius assertions remain the acceptance
oracles.

## Obsolete drag work

`DirectPreviewWorker` retains one pending request and one private evaluation
thread. Generation checks now skip obsolete work before source copying/native
evaluation, after evaluation, after tessellation and during display packing.
Cancellation never terminates an unrelated native job. The UI retains its
gesture, revision, document-epoch and source checks before accepting a result.

The deterministic 130-request regression previously evaluated the first and last
requests. With the same blocked-first-request input sequence, it evaluates only
the final request, coalesces 128 pending replacements and skips one obsolete
request before evaluation. A separate post-evaluation barrier proves obsolete
display preparation is skipped. The retained candidate applies with zero
additional feature evaluations and Undo restores the original dimension.
These are measured work counts, not claims about interactive latency, provider
tokens, cost or every pointer-event schedule.

## Verification entry points

- `surface-compatibility-074b`: rational/different-basis G0/G1/G2, rectangular
  trims, native tensor-product references, explicit closed cylinder network,
  joined/planar offsets, holed-face thickness, authored loft seams, native
  correspondence proposals and unsupported/stale-input rejection.
- `direct-preview-worker`: deterministic coalescing before and after evaluation,
  exact final geometry, prepared-candidate custody, revision/source/epoch guards,
  failed geometry, cancellation, one Apply and Undo.
- `--self-test-surface-advanced-ui`: actual forms, dependent controls, candidate
  images, rational Match, closed network, reviewed loft seam, both advanced
  thickness methods, Cancel/Apply/Undo and save/reopen. Normal and compact
  layouts require capture inspection before release acceptance.

New authored fields are conditionally gated by document source semantics 59.
Unchanged default recipes do not receive new fields just because they were
opened in a form. The physical document container version is independent.
