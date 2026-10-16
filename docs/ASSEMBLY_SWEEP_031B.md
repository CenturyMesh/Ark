# Sampled assembly and occurrence-aware routing — 031B

B.04/D.04 remain **Parcial** outside this bounded local scope. `assembly_info`
adds read-only `sweep` inspection of actual existing manipulation-joint poses.
It does not modify an authored placement, advance history or simulate forces.

```json
{"sweep":{"driver":"Slider","mode":"translate","from":0,"to":60,"samples":7,"pairs":[{"a":"Fixture","b":"Slider"}],"cables":[12],"collision_mode":"exact"}}
```

`from` and `to` are deltas relative to the current solved baseline, in degrees
for a rotational joint or millimetres for a translational joint. They are not
absolute joint coordinates. The requested endpoints are included in 2..65
uniformly spaced samples. Optional `mate` selects an existing joint explicitly;
optional `mode` is `rotate` or `translate`. Existing limits, grounded-joint
selection, passive motion and full constraint validation remain authoritative.
An unsatisfied pose, invalid reference, capacity failure or cable-route failure
rejects the entire query instead of publishing a clear prefix of the sweep.

## Collision evidence

`pairs` contains up to 64 unique unordered pairs of distinct occurrences, using
node IDs or unique names in `a` and `b`. Optional `copy_a`/`copy_b` select stable
pattern slots, defaulting to 0. Duplicate, missing or invalid slots reject.
The tool validates every sampled occurrence transform and records the actual
joint coordinate and maximum constraint residuals at each sample.

`collision_mode:"exact"` is the default. It uses the existing exact native
single-solid collision producer and reports actual common volume, minimum
distance and touching/interference status at the sampled poses. Unsupported
geometry rejects without a fallback. Explicit `collision_mode:"mesh"` uses
existing triangle collision/containment checks, retaining triangle authority,
sampling provenance and unresolved properties in each pair's measured report.
It does not reinterpret a mesh as a solid.

The result includes every requested sample, each pair's measurement, counts of
interfering and unknown pair samples, and explicit `sampled_poses_only:true`,
`between_sample_clearance_checked:false` and `dynamic_physics:false`. A clear
sample set cannot prove clearance between samples. Touching is preserved in the
underlying reports; the aggregate `all_sampled_pairs_separated_or_touching`
does not imply a required engineering clearance. A sweep with no pairs has no
collision evidence even though its aggregate empty-pair condition is true.

## Cable endpoint, waypoint and obstacle bindings

An authored `cable_route` can retain occurrence-bound points:

```json
{"start":{"node":3,"copy":0,"reference":"origin","offset":[0,0,20]},"end":{"node":8,"reference":"connector.face","offset":[0,0,2]},"radius":1,"bend_radius":4,"obstacles":[{"node":9}]}
```

`start`, `end` and individual `waypoints` accept `{node,copy?,reference?,offset?,name?}`.
Persistent `node` bindings require stable numeric IDs. Names remain available
only for ephemeral read-only sweep selections, so renaming an occurrence does
not invalidate a retained endpoint or obstacle recipe.
The default reference is the occurrence origin; other names resolve real
producer-authored exact geometry frames. The offset is a three-coordinate vector
of numbers or authored expressions in the reference frame. Resolution then
applies the actual occurrence transform to obtain a point in assembly world
millimetres. The route geometry is authored in that world frame; avoid applying
a second occurrence transform to the resulting route.

Each bound producer must be retained explicitly in the cable's `inputs`.
Binding to the cable itself, or to a feature whose dependency chain reaches
that cable, rejects. A nonexistent/suppressed/ambiguous occurrence, copy or
semantic reference rejects. Numeric/named legacy points remain supported;
routes with no occurrence binding require no assembly solve.

`obstacles` accept `{node,copy?,name?}` in addition to existing explicit AABBs.
Native producer bounds are used where available; otherwise evaluated geometry
bounds are used. The local box is transformed to an enclosing world AABB, so
the model is conservative and can reject routes that would pass around the
actual rotated/concave body. It never claims exact collision with the original
obstacle shape. Planar/zero-volume obstacle boxes reject without inferred
thickness. Duplicate bound obstacle slots reject. Limits remain 64 obstacles,
32 waypoints and the existing cable coordinate/radius/bend/length domains.

The cloned resolved recipe includes `occurrence_binding_report` with source
node/copy/feature, semantic reference, measured world endpoints, box authority,
world bounds and explicit conservativeness. The original source binding remains
editable and persists, rather than being replaced by evaluated coordinates.
These new authored bindings conditionally require source format 43.

## Routing at each sampled pose

`cables` names up to 32 unique unsuppressed cable feature IDs. At every pose the
tool resolves their retained bindings against the actual sampled placements,
then runs the existing cable normalization, bounded routing, obstacle-prefilter,
length and bend checks, followed by actual native swept cable construction and
exact distance checks against the declared or occurrence-derived AABB obstacles.
Each sample returns `exact_obstacle_checked:true`, the measured native route
report, geometry report and binding evidence. This catches rounded bends that
enter an obstacle while the incident straight centerline segments are clear.
The box representation remains conservative; exact swept-cable-to-box checks do
not imply collision checks against the original rotated/concave obstacle B-rep.

This is a sequence of static routes. It does not model elastic cable length,
strain, twisting, clips, connectors, thermal effects, forces, continuous collision
or a physical trajectory. At most 4096 combined pair/cable sample checks are
accepted per query. No partial report is published if any sampled route fails.

## Local evidence

`ark-assembly_sweep-031b-tests` exercises exact sampled box common volume and
distance, explicit mesh authority, joint limits and late-pose rejection,
nonmutation of authoring/revision, semantic endpoint resolution, explicit
dependency and self-dependency rejection, invalid/duplicate copy references,
conservative obstacle evidence, route length changes at moving endpoint poses,
legacy no-assembly routing, source43 persistence/recompute and old-format
rejection, and a cable failure after an initially clear sample. These are local
synthetic regressions, without user documents, dynamics or manufacturing claims.
The native suite also moves an occurrence while retaining the same source
feature definitions, verifies the resulting cable length, and verifies undo.
It rejects an actual rounded native bend that intersects a small inside-corner
obstacle even though the centerline polyline passes its obstacle prefilter.
Portable builds run legacy no-assembly binding/validation checks and explicitly
skip the native geometry sweep checks.

Local native execution on 2026-10-16 passed `ark-assembly_sweep-031b-tests`.
The four-pose slider fixture measured initial box overlap and final separation,
and changed the bound cable's measured length from 10 mm to
`sqrt(136) mm` after a 6 mm endpoint displacement. Committing that displacement
invalidated the route cache and undo restored 10 mm. The final run includes
actual native cable construction at every sampled pose and the rounded-bend
obstacle rejection. The final portable runtime suite also passed its explicitly
bounded validation branches as part of 76/76 portable CTest cases; native
integration passed 116/116.
