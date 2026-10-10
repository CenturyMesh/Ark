# Functional mechanical couplings

`agent_onboard{topic:"motion"}` introduces persisted instance couplings and
`assembly_motion`. Solve the static assembly with ordinary geometric mates
first; ground supports while leaving the rotating or translating part instances
free. Couplings relate motion coordinates and do not replace static mounts.

```json
{"op":"mate.add","a":"Front crank","b":"Rear sprocket","type":"chain",
 "reference_a":"Z","reference_b":"Z",
 "motion":{"teeth_a":"front_teeth","teeth_b":"rear_teeth"}}
```

Endpoints are assembly instance IDs or unique instance names. Select local
`X`, `Y`, `Z` or discover exact semantic reference axes with `reference_inspect`.
The worker derives world axes from the solved static instance placements.
Teeth counts are integer expressions from 1 to 10000 and save in version 22.
Existing geometric mates and version 21 sheets remain readable.

| Type | Persisted `motion` fields | Relation |
| --- | --- | --- |
| `chain` | `teeth_a`, `teeth_b`, optional `phase_deg` | Same physical rotation direction, speed ratio `teeth_a/teeth_b` |
| `gear` | `teeth_a`, `teeth_b`, optional `phase_deg` | External gears, opposite physical direction |
| `shaft` | None | Coaxial rigid rotation at 1:1 physical speed |
| `rack_pinion` | Positive `pitch_radius`, optional `direction` of 1 or -1 | Travel in mm = signed pitch radius × input angle in radians |
| `cam` | Nonnegative `lift`, optional `phase_deg` | Ideal harmonic follower lift, with velocity from its analytic derivative |

Chain/gear axes must be parallel; shaft axes must be coaxial. Opposite selected
axis directions change the signed local angular coordinate correctly. Rack and
radial cam travel must be perpendicular to the selected rotation axis. Use one
oriented semantic axis consistently per moving instance.

The cam law relative to its authored phase-zero position is
`d(theta)=lift/2*(cos(phase)-cos(theta+phase))`; its velocity is
`lift/2*sin(theta+phase)*omega`. Author an eccentric or profile consistent with
this law if displaying a physical cam. Arbitrary profile following and contact
are outside this model. `phase_deg` for angular couplings is a driven angular
offset from the authored static pose.

```json
{"tool":"assembly_motion","arguments":{
  "driver":"Front crank","speed_rpm":"cadence_rpm","time_s":0.25
}}
```

The read-only result includes all instance matrices in `world_column_major`,
rotating `angle_deg`/`speed_rpm`, or follower
`displacement_mm`/`velocity_mm_s`. Relations report evaluated teeth counts,
signed ratio and its scope, active status and `residual_rad` or `residual_mm`.
The engine propagates coupled equations through the graph and verifies the
retained geometric mate constraints at the requested pose. Incompatible
static constraints reject instead of producing an independent animation.
Document revision, static placements and undo history stay unchanged.

The bicycle example, [bicycle-transmission.arkops](../examples/bicycle-transmission.arkops),
couples `Front crank` to `Rear sprocket` by chain and attaches `Rear wheel` by
shaft. At 48/16 teeth and 60 rpm cadence, the rear sprocket and wheel turn at
180 rpm. Changing `rear_teeth` to 24 changes them to 120 rpm.
See [the bicycle walkthrough](BICYCLE_TRANSMISSION.md) for its dimensions,
teeth display geometry and teaching commands.

Cycles, multiple incoming drivers, translating followers used as rotational
drivers, fixed moving endpoints, patterned/group endpoints and inconsistent
semantic axes reject explicitly. Couple individual attached parts with shaft
relations. A disconnected component reports inactive. Input time is 0..3600 s,
speed is bounded to ±100000 rpm and initial offset to ±1e6 degrees. Propagated
angles/speeds are bounded to 1e9 rad/rad/s, travel to 1e6 mm and follower speed to
1e9 mm/s; larger requests reject before matrix construction.

This is an ideal kinematic model. It does not certify tooth geometry/contact,
chain routing/collision/tension/slack, freewheel behavior, cam contact, forces,
torque or dynamics. The result keeps those validation flags false.

For Studio, `buildAssemblyMotion(document, program, error)` owns an evaluated
static baseline and all resolved references. Cache the program by document
revision; call `inspectAssemblyMotion(program, driver, rpm, time, angle)` for
each display frame without copying or recomputing the document. The session
tool uses the same C++ functions with a revision cache.
