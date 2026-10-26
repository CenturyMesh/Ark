# Mechanical transmission suite — 061C

The same `AssemblyMate.motion` expressions now drive both the existing
kinematics and native engineering reports. The suite covers external/internal
gear pairs, spur/helical/double-helical forms, roller chains, synchronous and
smooth belts, coaxial compound transmissions and circular shaft torsion.
Results are analytical studies, not tooth-contact, fatigue or manufacturing
qualification. **B.04/C.04 remain partial overall.**

## One authored source

Use existing occurrence IDs and semantic axis references in `mate.add/edit`.
`gear`, `chain`, `shaft`, `rack_pinion` and `cam` retain their existing contracts;
`belt` is a new mechanical type. Geometry/positioning mates remain separate.
Rotational couplings require parallel oriented axes; shaft mates require a
shared axis. Compound stages can share one occurrence with two different tooth
counts at its input/output meshes, or use separate occurrences connected by
authored shaft mates. No additional hidden assembly graph is maintained.

The new engineering fields and the `belt` type conditionally require semantic
document format **54**. Legacy tooth-ratio-only mates retain their previous
format and behavior. Editing a field changes the document revision and journal;
calculating a report does not. Mate expressions and parameter dependencies are
visible in the existing `context_graph` and native project graph.

| Mate | Fields, in addition to endpoint/reference IDs |
| --- | --- |
| Gear | `teeth_a`, `teeth_b`, `module_mm`, `pressure_angle_deg`, `helix_angle_deg`, optional `helix_b_deg`, `face_width_mm`, `phase_deg` |
| Internal gear | Gear fields plus `internal:1`; B is the larger driven ring. |
| Double helical | Gear fields plus `double_helical:1`, `center_gap_mm`, optional `left_load_fraction`. Zero gap denotes herringbone. |
| Chain | `teeth_a`, `teeth_b`, `pitch_mm`, optional `links`, `phase_deg`, `allowable_chain_pull_N` |
| Timing belt | `synchronous:1`, `teeth_a`, `teeth_b`, `pitch_mm`, optional `belt_teeth`, `phase_deg` |
| Smooth belt | `synchronous:0`, `pitch_diameter_a_mm`, `pitch_diameter_b_mm`, optional `crossed:1`, `pretension_N`, `friction_coefficient`, `allowable_belt_tension_N` |
| Shaft | `diameter_mm`, optional `inner_diameter_mm`, `length_mm`, `shear_modulus_GPa`, `stress_concentration`, `allowable_shear_MPa`, `max_twist_deg` |
| Common | Optional `efficiency`, `service_factor`; non-shaft pairs also support `center_distance_mm`, `enforce_center_distance`, `center_tolerance_mm`, `length_tolerance_mm`, `axial_tolerance_mm`, `allowable_tangential_force_N`. |

Values are numbers or document parameter expressions. Flags in `motion` use
numeric 0/1. Efficiency must be in (0,1]; service and stress-concentration factors
must be at least one. Tooth counts are integers. Supplying engineering data
does not create a matching tooth profile: declared dimensions must agree with
the source CAD. A parameter binding is not evidence of physical contact.

For gears, `module_mm` is the **normal module**; it equals transverse module
for spur gears. A supplied pitch diameter can determine module from its tooth
count instead. If module and pitch diameters are both supplied, inconsistent
values reject. Parallel external helical gears require opposite declared hands;
an internal ring uses the same hand. Only zero profile shift is modeled.

The positioned reference-axis distance is measured separately from nominal
gear centre distance. `enforce_center_distance:1` rejects incompatible centres
and absent axial face overlap. It does not move parts or widen a tolerance.
The source references should identify the intended gear stations on a compound
part. The default centre comparison remains 1e-5 mm; an explicit tolerance is a
caller declaration, not an automatic recovery policy.

## Calculations and assumptions

- Spur reference diameter: `d = m*z`. For a helical gear, transverse module is
  `m_n/cos(beta)` and transverse pressure angle is
  `atan(tan(alpha_n)/cos(beta))`.
- External reference centre distance is `(d_a+d_b)/2`; internal is
  `(d_b-d_a)/2`. Standard tip/root/base diameters and zero-shift undercut/contact
  ratio screens are reported where supported.
- A roller sprocket uses `d = pitch/sin(pi/z)`. A timing pulley uses
  `d = pitch*z/pi`. These formulas are not interchangeable.
- Open/crossed circular-reference paths provide belt length and wrap. Chain
  length and fitted centre use the two-sprocket pitch-count approximation;
  even link proposals and polygonal speed variation are explicitly labeled.
  Sag, tensioners and actual tooth engagement are not solved dynamically.
  A declared chain link count reports the required centre adjustment; strict
  centre enforcement rejects a mismatching declared length. A declared timing
  belt length can likewise be checked against the reference loop using the
  explicit length tolerance.
- RPM follows the signed existing mate ratio. Torque is reported as a magnitude:
  `T_out = T_in*eta/abs(ratio)`, with `P = T*abs(omega)`. Negative torque/braking
  flow is not modeled. Power at zero speed cannot determine torque; provide
  torque directly for a static calculation.
- Tangential reference force is `2*T/d`; gear radial and helical axial forces
  use transverse pressure and helix angles. For chains, mean pull is derived
  from tooth-pitch advance; it is distinguished from the circular-reference
  tangential force.
- Double-helical halves are not assumed to balance physically. The ideal equal
  split is shown separately. A declared `left_load_fraction` gives each half's
  axial force and the residual magnitude; `axial_balance_verified` stays false.
- Belt pretension supplies tight/slack tensions. Flat-belt grip uses the smaller
  wrap and a common declared friction coefficient. It excludes groove wedge,
  centrifugal tension and material/temperature effects.
- Circular shaft section `J = pi*(D^4-di^4)/32` gives shear `T*r/J`. Length and
  shear modulus additionally give elastic twist `T*L/(G*J)` and stiffness.
  Lengths are mm, torque Nm, stress MPa and shear modulus GPa. Design load factor
  applies to stress/twist; local stress concentration applies to peak stress.
- Optional Lewis screening uses caller-supplied module-based factors defined by
  `Ft = sigma*b*m_n*Y`. It is not an ISO/AGMA contact/fatigue rating. The width
  excludes a double-helical central gap; a declared uneven half-load increases
  the more heavily loaded half's screen.

Missing efficiency does not become a measured default. The analysis returns
available kinematics while load results remain unavailable, unless the caller
explicitly supplies efficiencies or authorizes an ideal fallback. Similarly,
section-only shaft data can provide shear without inventing a twist.

## Agent workflow

Start with `agent_onboard{topic:"transmission"}` or
`transmission_design{action:"capabilities"}`. All other direct tool actions
require `expected_revision`.

`transmission_inspect{action:"inspect",...}` reads the actual mate graph and
reports dimensions, compatibility and compound occurrences. `action:"analyze"`
adds `driver`, optional `output`, `input_rpm` and either `input_torque_Nm` or
`input_power_W`. `assume_ideal_efficiency:true` is explicit permission to use
unity only where a mate has no efficiency. At a branching node, `power_splits`
must define fractions by outgoing mate ID and sum to one. No input power is
duplicated across outputs. Results describe the selected forward power path.

The same study is available through
`assembly_info{detail:"result",transmission:{action:"analyze",...}}`. It retains
the observed revision and existing kinematic solver checks.

`transmission_design{action:"analyze",stages:[{kind,motion},...],...}` compares
numerical stages before CAD exists. It supports mixed forms and shafts.

`action:"optimize"` searches one/two-stage integer tooth choices. Declare:

- `kind`, `target_reduction`, `relative_error`, tooth bounds and stage bounds;
- gear `modules_mm`, `face_width_mm` and `forms` (`spur`, `helical`, `herringbone`,
  `double_helical`), or chain/belt `pitches_mm` and centre distance;
- optional diameter, per-stage/total-centre, output-torque, contact-ratio,
  rotation-direction and axial-force limits, plus chain ripple/pull and
  chain/belt engagement or belt-tension limits;
- load, efficiency and caller strength factors when filtering on load capacity.

`center_distance_mm` fixes the gear centre when supplied; otherwise the search
calculates it. `second_stage_center_mm` and `second_stage_modules_mm` allow
separate second-stage choices. Forms are common across the stages in this
optimizer; mixed forms can be analyzed explicitly. Objectives are maximum pitch
diameter, summed centre distances or ratio error. These are radial/layout
metrics, not whole-assembly mass, cost or axial-package optimization.

The result includes ranked candidates and the best per form/topology. Search
uses objective-bound pruning and is limited to the declared grids (at most 128
tooth values, eight module/pitch values and two million evaluated configurations).
Capacity rejects instead of silently shrinking the search. Cancellation is
supported. No global geometric or strength optimum is claimed.

`action:"prepare"` takes a current candidate and `bindings`:

```json
{
  "parameters": [{"parameter":"gear_module","stage":0,"field":"module_mm"}],
  "mates": [{"mate_id":4,"stage":0},{"mate_id":5,"stage":1}]
}
```

Existing exact parameter-name expressions on mates are reused. Explicit
parameter bindings can also target numeric derived dimensions. Shared-parameter
conflicts reject. Two stages must share a compound occurrence or an existing
coaxial shaft path. The candidate revision and authoring hash must match.
Preparation returns unexecuted `preview_arguments`; it does not publish CAD.
Changed geometry fields require parameter bindings, unless the caller explicitly
requests `kinematics_only:true`. Inspect the actual parameter dependencies and
tooth geometry, call `doc_preview`, review the result, then `doc_apply`.
Normal gear preparation enforces pitch-centre compatibility. Source expressions
for dependent placements remain active unless explicitly rebound by the caller.

Hashes identify source/result content; they are not permissions or independent
proof of a tooth profile. Motion, drawing, preview and journal contracts retain
their existing owners. Planetary/differential cycles and multiple independent
drivers remain outside the current mechanical solver.

## Studio

Use **Model → Transmission**, or **Motion → Transmission suite** in the tool
gallery. The native suite inspects mates, edits geometry/load fields, analyzes
power, compares designs and prepares reviewed changes. It uses the same C++
tools as agents. Calculation runs on a private snapshot; a changed revision
marks the report stale. The existing mechanical-motion controls animate the
same couplings. Ideal kinematic playback does not deform a shaft by its computed
twist or simulate backlash and tooth impacts.

## Technical references

- [KHK reference gear dimensions](https://khkgears.net/gear-knowledge/gear-technical-reference/calculation-gear-dimensions/)
- [KHK double-helical and herringbone terminology](https://khkgears.net/new/gear_knowledge/gear-nomenclature/double-helical-gears.html)
- [Renold chain design guide](https://www.renold.com/upload/renoldswitzerland/roller_chain_designer_guide.pdf)
- [University of Illinois circular-shaft torsion](https://mechref.engr.illinois.edu/sol/torsion.html)
- [Harvard Euler–Eytelwein relation](https://sciencedemonstrations.fas.harvard.edu/presentations/friction)

These references support the analytical models. They do not establish a
material allowable, a supplier rating, physical alignment or manufacturing
qualification for a particular user design.
