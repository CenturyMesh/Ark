# 031B surface and sweep authoring — B.03

These retained features extend the existing exact surface tools. The recipes
are expression driven and conditionally require document source **43**; older
recipes retain their existing behavior and source version. All writes retain
the observed `expected_revision` and publish atomically. Inspect current exact
producer references before binding faces. No display triangle identifies a
support face or selects production geometry.

## Support constrained patches and blends

`surface_fill` keeps its explicit connected, closed `native.profile` or saved
sketch. Without support constraints it remains the existing G0 fill. Add
`native.constraints` to request G1/G2 against specific native support faces:

```json
{
  "op": "feature.add",
  "type": "surface_fill",
  "name": "Transition patch",
  "inputs": [12, 17],
  "native": {
    "op": "blend",
    "profile": {"entities": [
      {"kind":"bezier","points":[[0,0,0],[2,0,0],[4,0,0],[6,0,2],[8,0,2],[10,0,2]]},
      {"kind":"line","points":[[10,0,2],[10,5,2]]},
      {"kind":"bezier","points":[[10,5,2],[8,5,2],[6,5,2],[4,5,0],[2,5,0],[0,5,0]]},
      {"kind":"line","points":[[0,5,0],[0,0,0]]}
    ]},
    "constraints": [
      {"edge": 1, "support": 12, "face": "surface", "continuity": 2},
      {"edge": 3, "support": 17, "face": "surface", "continuity": 2}
    ],
    "validation": {"samples": 33, "tangent_deg": 0.001, "curvature_per_mm": 0.0001}
  }
}
```

This example assumes face `surface` of input 12 supports the edge at z=2 and
input 17 supports the edge at z=0. Choose actual inspected producer IDs and names.
The profile accepts supported line/arc/Bezier entities, a profile wire or a saved
sketch. `edge` is the
zero-based edge in that explicit boundary profile, not a mesh index. Each
support is an explicit feature ID also present in `inputs`; its semantic face
and topology digest resolve against that producer's current B-rep. Boundary
expressions, continuity and validation numbers recompute with the document.

An unconstrained boundary edge remains G0. A constrained edge must lie on its
trimmed support face. The native worker creates a private pcurve representation
without editing the support, requests the actual plate derivative order and
rejects unsatisfied measurements. Each edge has at most one support.
`native.op:"blend"` requires at least two distinct support faces; the remaining
connected boundaries stay explicit. There is no automatic gap discovery,
automatic choice of production edges or general rolling-ball blend claim.

Position tolerance remains **1e-6 mm**. Validation uses 3..129 uniform
edge-parameter checkpoints including endpoints. The default tangent-plane
angle limit is 0.001 degrees; explicit limits must be positive and no greater
than 0.1 degrees. The default curvature-tensor error limit is 0.0001/mm;
explicit limits must be positive and no greater than 0.01/mm. These angular and
curvature acceptance criteria do not relax the position tolerance.

`report.support_boundary_evidence[]` records the measured maximum position
error, unoriented tangent-plane angle and curvature-tensor error at each
supported edge. Curvature comparison uses the two surfaces' native D2 jets,
matching normal orientation and expressing both second fundamental forms in a
common tangent basis. The Frobenius norm checks directional curvature, including
the mixed component; matching principal-curvature magnitudes alone is insufficient.
G1 constrains position/tangency; G2 additionally constrains that measured tensor.
Singular surface checkpoints, off-support edges, stale references and failed
constraints reject the feature.

The kernel's G0/G1/G2 estimates are reported separately. In pinned OCCT 8.0.1,
`BRepOffsetAPI_MakeFilling` forwards its enum ordinal to GeomPlate's integer
constraint order, whose supported orders are 0/1/2. ARK passes the actual
integer derivative order through that API and checks the resulting derivatives
independently. It does not infer G2 from an enum name or successful API call.

Planar boundaries use a native planar face when valid. Four-sided boundaries
with two matching-degree opposite Bezier curves and two straight connectors
can use an exact tensor-product Bezier strip. Those constructions preserve the
explicit boundary geometry and undergo the same independent support-jet checks;
they do not infer continuity from the construction method. Other boundaries use
the support-constrained native plate. Its internal position-fit request is
1e-8 mm; public position acceptance remains 1e-6 mm. All boundary edges receive
native position checkpoints, including unconstrained G0 edges. Reports identify
the actual construction method and leave inapplicable kernel estimates null.

This is **sampled continuity evidence** on an exact native B-rep. It does not
bound between-sample extrema or certify continuous/global G1 or G2.
`global_g1_verified` and `global_g2_verified` remain false.

## Explicit untrim and extension

`surface_edit` accepts one open face input. Extract a selected semantic face
from a solid or shell first. The input's exact supporting surface remains the
authority.

- `native:{op:"untrim",bounds:[u_min,u_max,v_min,v_max]}` replaces the existing
  trimming loops with one explicitly authored UV rectangle, restoring removed
  regions and holes. The rectangle must include the original UV domain and add
  measured positive area.
- `native:{op:"extend",bounds:[...]}` extends that rectangular domain beyond at
  least one original UV bound. Analytic supports can be extended in their natural
  domains. A bounded NURBS cannot be evaluated beyond its natural domain by
  merely changing bounds.
- For bounded support extension use
  `native:{op:"extend",extension:{direction:"u_max",length_mm:"Extension",continuity:2}}`.
  Directions are `u_min`, `u_max`, `v_min`, `v_max`, positive lengths are at most
  100000 mm, and native requested derivative continuity is 1, 2 or 3. This calls
  the native bounded-surface length-extension algorithm on a private surface
  copy. `bounds` and `extension` are exclusive.

UV expressions are support parameters, not necessarily millimetres or normalized
fractions. Inspect the supporting surface before choosing them. Periodic domains
cannot exceed one period. Degenerate bounds, loss of an original region and
empty extensions reject. Domain replacement intentionally removes trimming holes;
the report states that behavior and the added area. An extended trimmed face
becomes the declared complete rectangular support domain.

Reports include original/result UV bounds, added area and the maximum position
error at 81 original-domain UV checkpoints. Length extension requests the native
C1/C2/C3 construction; those checkpoints establish original-support preservation
locally, without independently certifying global extension continuity.

## Mutual trimming

Use two distinct open face inputs and
`native:{op:"mutual_trim",keep_points:[[x1,y1,z1],[x2,y2,z2]]}`. The exact native
surface/surface splitter must divide **both** input faces into 2..64 regions.
Each keep point must lie inside exactly one region of its corresponding input,
on that face and away from the new intersection/boundaries. Coordinates are
expression driven and use the input features' shared local coordinate system.

The result is a compound containing both explicitly retained faces. It is not
automatically sewn or solidified. Per-input reports account for original,
retained and explicitly removed areas and check that all split regions conserve
the original area. Coincident, tangent, nonsplitting and ambiguous selections
reject. No unnamed fragment is silently discarded.

## Native sweep orientation

The existing sweep's default remains corrected Frenet. Explicit
`native.orientation` selects one kernel mode:

| Mode | Additional fields | Geometric meaning |
| --- | --- | --- |
| `corrected_frenet` | none | Native corrected Frenet transport |
| `frenet` | none | Native Frenet trihedron |
| `fixed_binormal` | `binormal:[x,y,z]` | Constant authored binormal direction |
| `support` | `support:<feature ID>` | Binormal defined by one exact supporting face's normal |
| `auxiliary` | `guide:<sketch/curve name or profile>`, `equivalence:"curvilinear"` or `"normal_plane"` | Native auxiliary-spine orientation, without changing section width to force contact |

A support dependency must also appear in `inputs`. Every path edge needs an
actual pcurve on that one trimmed support. A private parameterized path is built
and checked before selecting the kernel support mode. Auxiliary curvilinear
equivalence compares relative path/guide lengths; normal-plane equivalence uses
the intersection with the plane normal to the spine tangent. A missing such
intersection rejects through the kernel. Fixed binormals cannot be zero or
parallel to a path tangent; 33 native derivative checkpoints per edge screen
that singularity, without certifying between-sample extrema.

Two parallel analytic line spines define a constant auxiliary orientation
exactly. ARK uses that native constant frame and reports the analytic solver;
normal-plane equivalence additionally requires the finite guide to cover every
spine normal plane. Other auxiliary cases use the native guide trihedron.

The existing `sections` sweep keeps its authored station frames. Combining
explicit orientation with those legacy frames rejects instead of ignoring one
definition. Unsupported fields/mode combinations reject. Explicit modes use
fixed 1e-6 mm construction and boundary tolerances.

## Authored continuous twist and scale laws

An exact straight, open, single-edge spine and one planar authored section can
use this retained recipe:

```json
{
  "laws": {
    "interpolation": "linear",
    "stations": [
      {"fraction": 0, "twist_deg": 0, "scale": 1},
      {"fraction": "0.5", "twist_deg": "Twist/2", "scale": "MidScale"},
      {"fraction": 1, "twist_deg": "Twist", "scale": "EndScale"}
    ]
  }
}
```

The law is piecewise linear in actual spine arclength: twist angle interpolates
in radians, and uniform scale interpolates independently. The section's authored
frame origin is the scaling/twist center and is placed on the spine. Section
coordinates are retained in their authored frame. The implementation supplies
continuous analytic frame values/first/second derivatives and native scale laws
to the OCCT surface sweep; it does not substitute a few station profiles and
claim the intervening geometry follows the requested law.

There must be 2..32 strictly increasing stations including exact fractions 0 and
1. Positive scales lie in `(1e-6,1000]`; each interval can contain at most one
full twist and absolute authored twist is limited to 50 revolutions. Add stations
to express multiple turns. Derivative jumps at internal piecewise-linear law
stations remain authored; global smoothness across those jumps is not claimed.

This bounded law implementation accepts corrected Frenet or fixed-binormal
orientation as the **zero-twist reference frame** on that straight spine; the
authored twist then rotates that frame. Curved/multiedge/closed spines, support or
auxiliary orientation combined with laws, legacy varying sections combined with
laws, contact translation, `correction:false` and nonplanar sections reject explicitly. General
curved-spine twist laws remain outside this delivered scope.

Each interval/section-edge surface is approximated by a native B-spline sweep;
the kernel-reported fit error must remain within **1e-6 mm**. Faces and explicit
end caps are sewn without deleting faces or raising tolerances. Construction
requests an internal 1e-8 mm fit. Closed source edges are subdivided into finite
native half-edges without moving their geometry. Caps reuse the actual fitted
rim edges; before capping, two-way native point-to-wire measurements compare
those rims with the original positioned exact profiles at 17 checkpoints per
edge, enforcing 1e-6 mm. The maximum observed end-profile error and sample count
are reported. This preserves explicit fitted-edge custody and does not claim an
unmeasured continuous bound for the end profiles. A requested solid
must produce one valid closed shell and one solid. Reports distinguish the
continuous analytic target law from its fitted B-rep, retain the authored station
values, state the kernel fit estimate and keep
`continuous_error_bound_certified:false`. Self-intersection qualification remains
outside this construction contract.

## Verification scope

`tests/surfaces_031b_tests.cpp` covers planar G1/G2 supports, a nonplanar explicit
G2 blend between distinct planes, stale/off-support/duplicate constraints,
untrimmed holes, analytic and bounded-NURBS extension, mutual trimming and
per-input area conservation, all orientation modes, continuous scale/twist volume
and independent native point-distance checkpoints, rejected unsupported domains,
expression recomputation, source43 roundtrip/downgrade and atomic invalid edits.
On 2026-10-16, the local Windows LLVM-MinGW / pinned OCCT 8.0.1 focused CTest
run passed **7/7** in **4.61 seconds**: `native-surfaces-031b`, `native-surface`,
`native-surface-repair`, `surface-edit`, `native-geometry-019b`, `native-features`
and `spatial-sketch`. The new surface suite passed all eight stages in 0.53 seconds.
These are local native and document integration checks, not a deployment result.

Development runs exposed and rejected a curved plate fit with measured curvature
error 0.001684/mm against the default 0.0001/mm limit; tighter plate fitting still
failed its position requirement. The final explicit Bezier-strip construction
passes the same acceptance criteria. General plate requests remain subject to
those checks and can reject otherwise feasible boundaries. A circular swept
profile also exposed unmatched cap/rim topology despite sub-tolerance geometric
distances; shared fitted-edge custody resolves that case while retaining measured
agreement with the original profiles. No failed attempt was accepted by increasing
position, tangency or curvature limits.

Milestone **B.03** remains **Parcial** beyond these measured local domains.
These checks do not establish production deployment, another product's adapter,
continuous global surface qualification or manufacturing qualification.
