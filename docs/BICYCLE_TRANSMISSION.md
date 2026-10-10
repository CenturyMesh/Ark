# Bicycle transmission: visible teeth and coupled wheel motion

[The native document](../examples/bicycle-transmission.ark) is ready to open.
[Its replay](../examples/bicycle-transmission.arkops) builds the same compact
bicycle with a blue tubular frame, fork/handlebars, two spoked wheels, front
chainring, rear cog, pedal/crank and chain pitch envelope. Apply the replay to
an empty document at revision 0. The saved version-22 document preserves the
mechanical relations and their expressions.
The example has 31 native features and 13 assembly nodes. It is a teaching
model, with explicit geometric and kinematic limits.

The front and rear sprockets have **48 and 16 individually patterned tooth
solids**. Their discs, bores and rounded tooth representations are exact OCCT
B-reps. Every tooth has actual material and a gap to its neighbour; the tooth
count is tied to `front_teeth`/`rear_teeth`. Rounded teeth are visual count
witnesses, **not roller-chain manufacturing tooth profiles**. Changing the
parameters rebuilds the native geometry and motion ratio together.

`chain_pitch_mm=12.7` sets nominal pitch-circle radii through
`radius=pitch*teeth/(2*pi)`. The displayed chain is an exact circular-section
sweep on the two external tangent lines and the wrapping pitch-circle arcs.
It follows the edited sprockets. It represents the chain's pitch envelope;
it does not model discrete roller/link articulation, chain closure at an
integer link count, contact, tension, sag or tooth engagement. The frame is
a nominal tubular fabrication program, without weld, strength or fit approval.

## The motion relationship

`Front crank` and `Rear sprocket` are unpatterned rotating part instances.
The rear wheel includes its actual rim, hub and radial spokes. Grounded axle
mounts carry concentric and axial-distance mates. These retain the centres and
axis directions while leaving rotation free.

```json
{"op":"mate.add","a":"Front crank","b":"Rear sprocket",
 "type":"chain","reference_a":"Z","reference_b":"Z",
 "motion":{"teeth_a":"front_teeth","teeth_b":"rear_teeth"}}
{"op":"mate.add","a":"Rear sprocket","b":"Rear wheel",
 "type":"shaft","reference_a":"Z","reference_b":"Z"}
```

A chain transmits the same rotation direction. The shaft transfers the rear
cog's rotation to the wheel at 1:1:

```text
rear RPM = cadence RPM * front teeth / rear teeth
```

[The motion replay](../examples/bicycle-transmission-motion.arkops) performs
this read-only inspection:

```json
{"tool":"assembly_motion","arguments":{
  "driver":"Front crank","speed_rpm":"cadence_rpm","time_s":0.125}}
```

The default cadence is 60 RPM. Over one eighth second, the crank advances
45 degrees and the rear cog/wheel advance 135 degrees, at 180 RPM.
`instances[].speed_rpm`, `angle_deg` and `world_column_major` describe the
actual wheel motion; `relations` reports the chain and shaft residuals.
The static axle positions remain fixed. Inspection does not modify the
document or its revision.

## Change the gearing and compare

[The ratio-change replay](../examples/bicycle-ratio-change.arkops) changes the
rear cog to 24 teeth at revision 1. Adapt `expected_revision` after other edits.
Run the same motion inspection again. The rear geometry now has 24 teeth and
the wheel turns at 120 RPM for a 60 RPM cadence.

| Front teeth | Rear teeth | Cadence RPM | Ratio | Rear wheel RPM |
| ---: | ---: | ---: | ---: | ---: |
| 48 | 16 | 60 | 3 | 180 |
| 48 | 24 | 60 | 2 | 120 |
| 48 | 16 | 90 | 3 | 270 |
| 48 | 24 | 90 | 2 | 180 |

The example's native circular patterns accept integer counts from 2 to 64;
invalid counts fail the whole edit. Undo restores both the visible cog and its
ratio. More rear teeth lowers wheel speed at the same cadence. This is an
ideal imposed-speed relationship, without torque, friction, inertia,
freewheel/coasting or ground-contact dynamics.

`bicycle-transmission` CTest loads this exact replay and checks native tooth
material/gaps, tooth copy counts, exact chain validity, wheel speed and matrix
rotation, axle retention, cadence/gearing regeneration, failed-edit rollback,
read-only inspection, persistence and undo. It does not certify the simplified
tooth/chain shapes for fabrication or establish physical drivetrain efficiency.
