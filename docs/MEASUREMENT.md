# Measurement and display units

Open **Inspect → Measure 2D**, **Measure 3D**, or **Units**. The same commands
are searchable in All tools. Measurements are read-only, temporary viewport
annotations; they do not create features or modify dimensions.

**Measure 2D** uses the active sketch plane (XY when no other plane is selected).
The camera aligns to that plane. Click A and B to see signed X/Y components
and planar distance. Existing sketch endpoints/centres and the visible grid
can acquire the pointer. The plane name and point sources remain visible.

**Measure 3D** uses world coordinates, including displayed assembly placement.
Click two surfaces to see signed X/Y/Z components and spatial distance. Native
bodies resolve the camera ray against the exact B-rep and can snap to a nearby
visible native vertex. Pixel acquisition controls which vertex is selected;
it does not change kernel tolerances. An arbitrary surface click is an exact
intersection of the selected camera ray, not a claimed design dimension or
minimum distance between entire faces. Mesh-only objects are labelled
**Display mesh estimate**. A failed native intersection is explicitly rejected.

**Coordinates** accepts two explicitly entered points in the selected length
unit. For 2D these coordinates are relative to the displayed plane and Z must
be zero; for 3D they are world coordinates. A third click starts another pair.
Close, Escape or right-click exits. A changed document revision or display-only
fold/motion preview clears the measurement rather than showing stale evidence.
Return to the stationary formed model to measure it. Tube hinge previews are
not silently measured as their undeformed B-reps.

**Units** saves application preferences for measurement, native geometry
properties, physical mass/centre-of-mass displays and grid labels:

| Quantity | Default | Choices |
| --- | --- | --- |
| Length | mm | mm, cm, m, in, ft |
| Mass | g | mg, g, kg, lb |
| Volume | L | mm3, cm3, mL, L, m3 |
| Significant digits | 6 | 3–9 |

Area follows the square of the length unit. `1000 mm3 = 1 mL`,
`1000000 mm3 = 1 L`, `1000 g = 1 kg`, and `25.4 mm = 1 in`.
Conversions change presentation, not existing geometry. Authoring forms whose
labels specify mm continue to accept mm. Agent/API contracts, exports and
saved model values retain their explicit canonical units; no document schema
change or rescaling is performed. Preferences live in the existing Studio
settings, not in individual drawings. These measurements do not certify
manufacturing tolerances or model validity.
