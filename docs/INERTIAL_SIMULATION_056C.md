# 056C inertial dynamics

Milestone **C.04**, **Em curso** pending final integrated verification. This is a
derived rigid-body study in the ARK design workspace. The study does not replace
the B-rep, update assembly placements, qualify manufacture or claim superiority
over another simulation product.

## Inputs and authority

`physics_inspect` retains its static physics contract. Its explicit `simulation`
argument runs a separate time-dependent study. A run requires the current
`expected_revision`; static fields such as top-level `id`, `gravity` or `loads`
cannot be mixed with a simulation and silently ignored. Query
`simulation:{action:"capabilities"}` first. The `physics` onboarding guide and
`agent_onboard{topic:"commands",query:"simulation"}` expose the actual schema.

```json
{
  "expected_revision": 12,
  "simulation": {
    "backend": "gpu",
    "duration_s": 1,
    "step_s": 0.004166666666666667,
    "output_fps": 30,
    "solver_iterations": 16,
    "gravity_m_s2": [0, 0, -9.80665],
    "ground_z_mm": 0,
    "restitution": 0.1,
    "friction": 0.5,
    "bodies": [{"node": 123, "copy": 0, "force_N": [0, 0, 0]}]
  }
}
```

IDs in that example are placeholders for observed occurrence IDs and revisions.
Select an occurrence using `node` and `copy`, or a uniquely placed definition
using `id`. Omitting `bodies` selects visible assembly occurrences, or active
unconsumed definitions without an assembly. Missing density or native mass
properties rejects the study. Native mass, centre of mass and centroidal inertia
come from the existing exact CAD producer and declared density; heterogeneous
PCBs do not become fictitious uniform solids.

Body overrides accept `fixed`, `velocity_m_s`, `angular_velocity_rad_s`,
`force_N` and `torque_Nm`. Forces and couples are constant in world axes and apply
at the centre of mass. A fixed occurrence or fixed ancestor stays grounded unless
the study explicitly overrides it. The solver uses metres, kg, seconds, N and
Nm; CAD positions and ground height remain mm. Fixed bodies cannot have velocity.

## Solver and meshes

The Windows GPU backend uses a hardware Direct3D 11 compute device, shader model
5. Integration, convex separation-axis tests, contact impulses, friction and
joint iterations execute on the GPU. The CPU prepares exact mass properties,
validates meshes, schedules work and reads output frames. This is not GPU B-rep
evaluation. GPU unavailability or failure is explicit; `backend:"cpu"` selects
the portable float64 reference solver, with no silent fallback.

Translation integrates the declared constant external force; rotation uses a
midpoint gyroscopic step and normalized quaternions. Contacts use discrete convex
SAT with face axes and cross-edge axes. Each GPU pair has one canonical normal
and opposite impulses. The contact point averages distinct clipped intersection
vertices, with coincident endpoints counted once (1 micrometre comparison).
This avoids weighting shared vertices by edge multiplicity. Failure
to find an intersection witness rejects the result. The response uses a single
contact point, restitution, Coulomb friction and relaxed Jacobi iterations.
It is not a multi-point persistent contact manifold or continuous collision
detection. Resting stacks, high-speed contact and complex friction require
additional convergence and validation.

Collision meshes must be finite, closed, consistently outward and convex. Their
positions are the current derived display mesh; exact mass remains separately
authoritative. Concave solids are rejected, without replacing them with a convex
hull or discarding geometry. Exact duplicate vertices and coplanar triangle
diagonals are removed from the collision representation. One definition supplies
all its selected occurrences. GPU mesh buffers are cached by exact packed bytes;
an unchanged subsequent study uploads no mesh bytes. The separate viewport
cache compresses normals, and persistent derived storage uses lossless zstd;
see [viewport](VIEWPORT_056C.md) and [storage](PROJECT_SCALE_COMMANDS_056C.md).

Unlimited revolute mates between leaf occurrences in the same carrier scope
are supported. Joint anchors/axes come from the current CAD mate solve. Limited,
prismatic, rigid/coupled, cross-group carrier joints and patterned bodies reject until
their dynamic contracts are implemented. `assembly_joints:false` explicitly
requests detached bodies; it is never inferred after a failed mate. This scope
does not include deformable parts, structural FEA, stress, thermal analysis,
fatigue, actuator control or external solver qualification.

## Resource, revision and result contracts

The initial scope supports 128 bodies, 128 joints, 4,096 unique vertices per
convex shape, 131,072 vertices per study, 60 seconds, 14,400 actual frame-aligned
steps, 120 output frames/s and 65,536 stored body states. Requested JSON frame
output is bounded to 32,768 body states. Total worst-case collision work is
validated before dispatch, in addition to per-pair complexity. Overflow rejects
with a concrete cause. These are solver resource contracts, not agent budgets.

The host cancellation policy is checked during CPU/GPU stepping and while
waiting for the shared GPU device. GPU state is float32, including contacts;
the reference path is float64. Invalid inertia, non-unit quaternions, nonfinite
geometry/state and unsupported numeric ranges reject. The native worker's
existing exact geometry cancellation and revision guards remain in force.

Results carry the observed revision, source hash, explicit input study,
backend/device, transfer-inclusive elapsed time, GPU mesh uploads/cache hit,
initial/final state and energy, and maximum sampled joint-anchor drift. Optional
frames contain column-major display transforms with occurrence IDs. They are
not authored assembly changes. Refining the time step and comparing analytic
cases is required for new use cases; `qualified:false` remains explicit.

## Verification record

Final integrated commands/results are recorded in [056C](iterations/056C.md).
Development failures are retained: the first GPU shader compilation rejected
the HLSL-reserved word `point`; it was renamed. Independent review identified
coincident-pair momentum asymmetry, invalid peripheral support contact points,
partial GPU initialization, frame-aligned resource accounting and unsupported
group carriers. Corrections have dedicated regressions or explicit rejection.
The peripheral-support regression initially still found edge-multiplicity bias
in the clipped witness; deduplicating endpoints corrected it without relaxing
the angular-velocity assertion. Worst-case GPU work includes that deduplication.
Partial selection of a carrier sibling/nested descendant and a pattern copy
without its seed also reject; selecting fewer endpoints cannot hide constraints.

The integrated native test currently passes 58 assertions on both CPU reference
and the NVIDIA GeForce RTX 4070 Ti: analytic free fall, constant force, torque,
torque-free asymmetric inertia, fixed bodies, restitution/momentum, coincident
contacts, peripheral support, ground/friction, hinge drift, repeat-run mesh cache,
exact native mass, revision/authoring preservation and explicit invalid-input
rejections. The 0.7 s single-body ground fixture took 3.082 ms on CPU and 12.197 ms
on GPU in one local run (168 steps, 3,024 dispatches). This small case shows GPU
overhead, not a speedup. It does not qualify large assemblies or general contact
accuracy; no superiority or broader performance claim follows from these tests.

Implementation references: [Microsoft compute shaders](https://learn.microsoft.com/en-us/windows/win32/direct3d11/direct3d-11-advanced-stages-compute-create)
and [Baraff/Witkin rigid-body course](https://www.cs.cmu.edu/~baraff/sigcourse/).
They describe the API and mechanics background; they are not evidence that this
implementation has been independently qualified.
