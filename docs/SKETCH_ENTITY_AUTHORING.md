# Typed sketch entities and spatial sweep authoring

This contract extends **B.02** (sketch authoring) and **B.03** (native curves and
sweeps). The typed source stays editable. Native B-rep geometry, explicit inputs
and producer references remain the geometry authority; a viewport mesh never
repairs or substitutes for a failed native operation. These capabilities need
document schema **34** when used. Older documents retain their existing schema.
The document schema is independent of the application's release identity.

## Sketch source and editing

An entity uses `kind: "recipe"`, a unique `name`, optional `construction`, and a
typed `recipe`. Coordinates and scalar dimensions accept finite numbers or named
expression strings. The recipe stores the authored inputs. Recompute derives
handles, display samples and native primitive DTOs without replacing that source.
Do not submit or patch resolved `dof` values on a recipe entity. Change its source
through `sketch.entity.set` or replace the sketch through `sketch.set`, with the
current `expected_revision` in `doc_apply`.

```json
{
  "op": "sketch.entity.set",
  "name": "Section",
  "entity": {
    "kind": "recipe",
    "name": "slot",
    "recipe": {
      "type": "straight_slot",
      "start": ["-L/2", 0],
      "end": ["L/2", 0],
      "width": "W"
    }
  }
}
```

All fields listed below are required; unknown fields reject. Angles are degrees,
lengths are millimetres, and pairs are local sketch `[x,y]`. The entity dialog
accepts `x,y` pairs and `x,y; x,y` point lists. Selecting an entity and opening its
coordinate editor edits these source fields. Named parameters recompute the
recipe and every dependent native consumer.

| Recipe type | Authored fields | Native geometry and limits |
| --- | --- | --- |
| `corner_rectangle` | `first`, `second` | Two opposite corners; four lines; nonzero width and height. |
| `center_rectangle` | `center`, `corner` | Centre and one corner; four lines. |
| `three_point_rectangle` | `first`, `second`, `third` | First side and a height point; the third point determines signed perpendicular height. |
| `three_point_center_rectangle` | `center`, `side_midpoint`, `corner` | Centre, midpoint of one side and a height point. The centre-to-midpoint direction defines orientation and half width; perpendicular corner displacement defines half height. |
| `parallelogram` | `first`, `second`, `third` | Three consecutive vertices; the fourth is derived; zero area rejects. |
| `straight_slot` | `start`, `end`, `width` | Two cap centres, two lines and two exact semicircles. |
| `center_slot` | `center`, `end`, `width` | Midpoint and one cap centre; the opposite cap centre is derived. |
| `arc_slot` | `center`, `radius`, `a0`, `a1`, `width` | Circular centre path with round end caps. Width must be below the centre-path diameter; a full-turn centre path rejects. |
| `three_point_arc_slot` | `start`, `through`, `end`, `width` | Exact circle through three distinct noncollinear points plus round caps. |
| `polygon` | `center`, `radius`, `sides`, `angle`, `inscribed` | 3..64 sides. `inscribed:false` uses vertex radius; `true` uses the radius of the circle tangent to the sides. |
| `perimeter_circle` | `first`, `second`, `third` | Exact circle through three distinct noncollinear perimeter points. |
| `ellipse` | `center`, `major`, `minor`, `angle` | Semiaxes, major >= minor > 0; four exact rational quadratic spans. |
| `partial_ellipse` | Ellipse fields plus `a0`, `a1` | Signed nonzero parameter sweep up to one full turn. |
| `parabola` | `vertex`, `focal`, `angle`, `t0`, `t1` | Exact quadratic segment with local `x=t²/(4*focal)`, `y=t`; `t1>t0`. |
| `conic` | `start`, `control`, `end`, `weight` | Exact rational quadratic with endpoint weights 1 and positive middle weight. Noncollinear controls required. Weight below/equal/above 1 is elliptic/parabolic/hyperbolic. |
| `style_spline` | `controls` | 2..26 Bezier control points; native degree 1..25. These are control points rather than interpolating anchors. |
| `equation2d` | `x`, `y`, `t0`, `t1`, `samples`, `tolerance` | Adaptive authored polyline sampled from the two expressions. Its native geometry consists of exact line edges; it is an approximation of the mathematical equation. |
| `point` | `point` | Editable local point handle; no profile edges. The point dialog creates construction geometry. Existing `kind:point` remains available for loft point sections. |
| `centerline` | `start`, `end` | Construction line; never included in a solid profile. |
| `midpoint_line` | `center`, `end` | One endpoint is derived by reflection about the midpoint. |
| `route` | `points` | 2..64 planar points joined by editable straight edges. This does not implement a routing product or fittings engine. |
| `belt` | `first`, `second`, `radius1`, `radius2` | Two separate circular pulleys with external tangent lines and wrap arcs. Crossed belts, more pulleys and a transmission solver are outside this recipe. |
| `text` | `origin`, `text`, `height`, `spacing`, `angle` | Built-in seven-segment stroke engraving font for A-Z, 0-9, spaces and hyphens; lowercase is displayed as uppercase. It is construction geometry with separate strokes. Installed font outlines, filled text and text extrusion are not supplied. |

The existing point, line, circle, interpolating spline, arc, continuous wire,
projection and offset entities remain available. Recipes do not change their
numeric kind identities. Inspection returns `recipe`, `recipe_report`, driven
handles and the actual `geometry_authority`; samples are display evidence.

For lines, arcs and positive-weight Bezier spans, display subdivision uses an
analytic sagitta or convex-hull bound. Equation curves use observed checkpoints
instead. Closed native profiles must be explicitly connected and valid. Open,
branched, degenerate or self-intersecting profiles reject when consumed; there is
no automatic closing, healing, tolerance increase or removal of failed solids.

## Spatial interpolating spline and spline on a surface

A spatial curve feature can contain an interpolating spline segment:

```json
{
  "op": "feature.add",
  "type": "curve",
  "name": "Spline3D",
  "native": {
    "start": [0, 0, 0],
    "segments": [
      {"kind": "spline", "points": [[0,0,0], [15,0,30], [10,10,65], [0,20,100]]}
    ]
  }
}
```

The segment accepts 2..64 XYZ points and must begin at the preceding endpoint.
OCCT interpolates a native B-spline with its 1e-7 mm construction tolerance.
Coordinates and parameter expressions remain in the feature source. Consecutive
coincident anchors and disconnected segments reject. Existing rational NURBS
segments remain available when explicit control poles and weights are required.

A surface spline is a UV Bezier pcurve on one producer face:

```json
{
  "op": "feature.add",
  "type": "curve",
  "name": "Spline on surface",
  "inputs": [1],
  "native": {
    "op": "surface_spline",
    "controls": [[0.1,0.1], [0.3,0.8], [0.7,0.2], [0.9,0.9]]
  }
}
```

The input must have exact native faces. A target with one face needs no selector;
multiple faces require `face` with a valid semantic reference. The recipe accepts
2..26 UV controls and optional matching positive `weights`. UV values use the
producer surface's parameter domain, which is not generally 0..1.

The native pcurve and producer surface are authoritative. OCCT builds its 3D
representation at 1e-7 mm tolerance. The report states this distinction, and
records 257 UV containment checkpoints. These checks reject observed excursions
outside the selected trimmed face; they do not prove global containment between
checkpoints. This is separate from directional projection of an existing exact
curve onto a surface, which remains available as `project_surface`.

Generic spatial point constraints and `start_ref`/`to_ref`/`through_ref` anchors
apply to authored line, arc, NURBS and interpolating spline segments. Derived UV,
projected and equation curves do not expose those point-solver handles; submitting
such fields rejects explicitly and preserves the document and its revision.

## Equation curves: exact typed sources and measured approximations

Generic XYZ equations retain `x`, `y`, `z`, `t0`, `t1`, and `samples`. Initial
sampling requires 2..512 points. Optional `tolerance` defaults to 0.01 mm and must
be 1e-6..5 mm. The native fitter uses explicit equation parameters and checks seven
stations per interval against the source expressions. Intervals that fail are
refined and the native B-spline rebuilt, up to 4096 points and 16 fitting passes.
Failure to meet the requested observed error rejects the operation.

The native report includes `equation_samples` (authored count),
`equation_resolved_samples`, `equation_refinements`,
`equation_checkpoint_evaluations`, `equation_observed_max_error_mm` and
`equation_requested_observed_tolerance_mm`. It always sets
`equation_between_samples_bound_verified:false` for arbitrary expressions.
Finite checkpoints can miss oscillations or narrow features, so a small measured
error is not a mathematical global bound or manufacturing qualification.

`equation2d` uses the same source evaluator to create an explicit native polyline
with observed chord-deviation evidence at seven stations per interval. This
source approximation is independent of viewport tessellation. Its inspection
authority is `authored_adaptive_polyline`.

For supported exact cases, select a typed `analytic` source instead of generic
`x`/`y`/`z` expression fields:

```json
{
  "op": "equation",
  "analytic": {
    "kind": "polynomial",
    "x": [0, 1],
    "y": [0, 0, "curvature"],
    "z": [0]
  },
  "t0": 0,
  "t1": 60
}
```

Polynomial coefficients are in ascending powers of `t`. Each axis has 1..26
coefficients, accepts named expressions, and defines a common degree <=25.
The evaluated power basis is transformed to exact native Bernstein controls over
the requested interval; the constant-point case rejects. This source does not
use interpolation samples. The **Exact polynomial XYZ** dialog exposes these
coefficient fields.

Curve length reports integrate derivatives of the native edges with the OCCT
abscissa integrator at requested 1e-8 mm tolerance. This improves measurement
accuracy without changing any production curve or the modeling tolerance.
`length_authority` and `length_integration_tolerance_mm` state that method.

Exact analytic circles and ellipses use `analytic.kind:circle|ellipse`, `center`,
`normal`, `x_axis` (XYZ), and `radius` or `major`/`minor`. Axes must be
perpendicular. `t0`/`t1` are radians and may span at most one full turn. The report
returns `equation_authority:exact_analytic_polynomial|circle|ellipse` and zero
equation samples. These exact typed cases avoid the arbitrary-expression
sampling limitation without claiming an unsupported symbolic evaluator.

## Varying sections on a spatial path

A sweep accepts either one existing `section` or 2..32 typed `sections`, never
both:

```json
{
  "op": "feature.add",
  "type": "sweep",
  "name": "Varying sweep",
  "native": {
    "path": "Spline3D",
    "sections": [
      {"section": "Start section", "fraction": 0},
      {"section": "Middle section", "fraction": 0.5, "roll": 15},
      {"section": "End section", "fraction": 1}
    ]
  },
  "p": {"solid": 1}
}
```

Fractions increase strictly from an explicit 0 to an explicit 1 and refer to
exact native path arc length. Each local section is positioned at that path
point with its plane normal aligned to the exact path derivative. The default
in-plane X direction is transported by minimal rotation between the authored
station tangents. Optional station `x_axis:[x,y,z]` supplies an explicit world
guide, projected perpendicular to the tangent; parallel guides reject. `roll`
rotates the section about its tangent in degrees. Ambiguous opposite consecutive
tangents require an explicit guide.

The source sketches, section fractions, rolls and path points remain editable.
Changing them recomputes the native sweep and its station frames. Closed native
sections are required for `solid:1`. OCCT must create a valid shell or solid;
failure rejects without a mesh fallback. The report includes `varying_sections`,
`dynamic_tangent_frames`, and
`station_authority:exact_path_arclength_and_derivative`. Station frames are
recompute evidence rather than automatically published construction datums.
Associative construction frames between authored points use the separate
`between_points` datum recipe.

## Verification scope

`tests/sketch_entities_tests.cpp` contains recipe validation, parameter
recompute, source-only round trips, schema 34 and atomic rejection fixtures. Its
native scope checks known rectangle/slot/circle/ellipse extrusion volumes,
varying-section straight and curved spline sweeps, UV surface curves, adaptive
XYZ fitting and exact polynomial/circle equations. The compiled OCCT 8.0.1 native
fixture executable passed locally after the dispatch and length-measurement
corrections. This includes all 23 recipe defaults, parameter/source custody,
degenerate style-spline rejection, ten known extrusion volumes, constrained and
varying-section spatial splines, native UV surface curves, unsupported derived
constraint rejection, and both equation authority paths. Full repository suite
results and Studio screenshot evidence belong in the central iteration record.

These fixtures provide local geometry and persistence evidence. They do not
establish production deployment, inter-product integration, global mathematical
bounds for arbitrary equations or manufacturing qualification.
