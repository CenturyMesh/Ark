# Design quality investigation

**A.03/D.02 — Em curso.** Source-only follow-up to 075D; no new distributed
iteration or measured design-quality improvement is claimed.

The current provider policy and static onboarding share one design-brief rule.
An editable concept is a starting checkpoint. The agent must develop it to the
requested detail and function, account for requested components and defining
shape details, and inspect the authored result before finishing. A rejected
operation calls for diagnosis and an equivalent valid construction, or an
explicit unresolved limitation. It does not authorize silent loss of intent.
Engineering justification must connect calculations to authored geometry and
distinguish measured inputs, sourced evidence and assumptions. CAD/BOM success
alone does not establish performance.

This is guidance, not a new automatic completion gate or geometry validator.
Read-only role permissions, concept-first authoring, conditional manufacturing
checks, atomic publication and revision guards remain in force. There is no new
turn/token budget. HTTP, Claude Code and ACP build their provider instructions
through the same request builder; static guides use the same policy text.

## Acceptance evidence still required

### Historical public example replay

`examples/airplane.arkops` entered Git at `f6699e5` on 4 October 2026.
Its 62 commands author six parameters, 24 planes, 25 sketches and seven
features: six lofts and one revolved surface. The fuselage uses seven sections;
the wing uses five. This establishes an earlier authored geometric example,
not the original model, user brief or provenance of its agent interaction.

An isolated session of the installed 075D MCP executable accepted the original
command file unchanged. All seven features evaluated without failure and all
seven native property reports returned `brep_valid:true`. Six outputs are solids;
the spinner is the explicitly authored surface. The first observed recompute
took about 1.37 seconds; this single run is not a performance comparison.
The current software snapshot was inspected. This confirms that the current
runtime can still construct this example, not identical topology to an old
binary, autonomous agent performance, printability or flight performance.
Receipts and executable/source hashes: `out/verification-076d/example-replay`.
No live Studio document was loaded or changed by these isolated replays.

The literal main provider instructions at `f6699e5` contain 891 characters
(133 whitespace-separated words); the released source at `4870937` contains
8,952 characters (1,211 words), excluding tool schemas, onboarding and history.
The source follow-up's shared guidance change is excluded from this count.
This is a measurable input change to investigate, not a causal attribution.
There has been no matched live model comparison or instruction ablation.

Compare historical and current behavior with identical briefs, reference inputs,
model/version, effort, connector and initial documents. Retain every attempt,
including rejected operations and abandoned constructions. Use isolated
synthetic cases; do not publish private drawings or conversations.

Review outcomes against the original brief: component and detail coverage,
proportions and interfaces, valid editable geometry, preservation of intent
after failures, and evidence supporting requested functional claims. Do not
score designs by primitive count, surface count or visual complexity alone.
For image reconstruction, compare visible details as well as silhouette and
proportions; hidden geometry remains an assumption. Keep independent human
review separate from machine-checkable properties.

The existing [agent benchmark](AGENT_BENCHMARK.md) supports retained attempts
and qualified acceptance. Offline scripted transports test connector behavior;
they cannot establish model reasoning, reconstruction fidelity or restoration
of historical design quality. The causal regression and matched live quality
comparison remain unresolved.

## Faceted display and curved authoring

This source follow-up examined why agent-made geometry looked faceted or
"octagonal" and why bold curved shapes were rare. It changes display and
guidance mechanisms only. It does not claim that agents now design better: no
live model run, instruction ablation or blind review was performed.

A cross-binary probe (051A, 055D, 056C, 066D and the 075D source) found identical
native geometry and display meshes. Thirteen probe bodies (cylinder, sphere,
filleted box, legacy and native circle extrusions, arc slot, closed spline,
legacy and exact revolves, loft and a 1 m cylinder) gave the same triangle and
B-rep face counts and volumes on every binary. The public airplane example gave
257,664 triangles and a byte-identical snapshot on 066D and 075D. The
mechanisms below are older than these binaries, not a 051A-to-075D regression.
Fillet, loft, sweep, revolve and extrude gained no new default-path rejection
between 066D and 075D.

**Drawn edge outlines.** Curved B-rep edges were drawn from chord deviation only.
With the 043A size-relative deflection, a r=6 round on a 1.2 m body was drawn
with 22.5-degree chords. At the 0.5 mm cap (bodies of about 5 m and larger) a
r=6 hole outline had eight chords, an octagon over a 0.25 rad shaded bore.
Outlines are now also split until each chord turns no more than the surface
angular deflection. This is best effort within the existing 16,384-point bound
and never rejects; cusps of C0 curves stop at short chords. The 5 m hole
outline now has 32 chords. `native-solid` checks the turn per chord on 0.2 m
and 1.2 m filleted plates and the chord count of a hole in a 5 m plate. With
the split disabled, the same checks failed (0.17 and 0.39 rad turns; polygonal
hole). On eight public synthetic shapes, old and new display meshing at the
current settings (median of five runs, same machine) produced identical
triangles. Drawn edge points rose, for example from 336 to
528 on the 1.2 m plate, 100 to 192 on a 1 m r=10 cylinder and 282 to 354 on a
torus. Meshing time differences stayed within run-to-run variation.

**Shaded surfaces.** The 043A parameters are unchanged. On straight r=6 rounds,
the widest shaded triangle spans 3.3 degrees (0.12 rad, up to 250 mm) and
6.9 degrees (0.25 rad, 1.2 m plate), at least twelve facets per quarter round.
The investigation's "about six chords" matched the drawn outline, not the
shaded faces. Restoring 0.12 rad for all sizes was measured on public synthetic
shapes, same machine and build, median of three runs. The 1.2 m plate went
from 1,716 to 6,628 triangles. A R=500/r=4 torus went from 18,360 to 37,800
triangles and from 190 to 780 ms. A r=2 tube swept along a 1 m spline went from
56,354 to 210,972 triangles and from 0.76 to 3.4 s. The 043A measurement used a
private document that is not re-run here, so the surface parameters were kept.

**Agent snapshot shading.** The software `snapshot` lit each triangle with its
own normal, so curved display meshes showed facet bands. Agents review their own
work with this image. It now interpolates the display mesh's vertex normals, as
Studio does for exact bodies. Planar triangles keep identical pixels. Bodies
Studio draws as hard surfaces stay faceted: legacy `extrude`/`sketch_cut`,
whose walls are sampled facets, and `notched_tube`, whose welded planar mesh
would otherwise bend flat faces. Receipts report `shading`, and
`appearance_scope` no longer claims flat lighting. `snapshot-032d` compares
flat and smooth renders of a 24-sided cylinder and of welded and unwelded cubes.
Through the tool, a 1 m exact rod shows no facet steps, while a flat render of
its own display mesh shows three. A notched tube's tool pixels equal a flat
render of its display mesh, which a smooth render does not. See
[032D](SNAPSHOTS_032D.md).

**Sampled authoring paths.** Legacy `extrude`, `revolve` and `sketch_cut`
build mesh solids whose curves are chords at the sketch tolerance. The
preloaded sketch guide's only worked example used legacy `extrude` for a
closed spline. It now uses `native_solid` `extrude`, which keeps six exact
spline side faces, and its follow-up hole uses a native circle cutter. The
guide states what the legacy types build and gives the exact
`native_solid`/`revolve_surface` syntax. `equation2d` is an authored
polyline. Its native extrusion exceeds the 512-name topology registry, and the
unchanged rejection now names the exact recipes instead of implying that fewer
samples help. The preloaded solids guide names `revolve_surface`, `loft` and
`sweep` syntax for designs that need them, without making curves a
requirement. The preloaded pack grows by about 1.2 k characters. The guides were
not deduplicated: `onboarding-current-075d` requires `geometry_extensions` in
every audited topic. `parametric-sketch` asserts the solids rule and the sketch
rules, and executes the follow-up hole parsed from the guide's own `next_step`
text, so a changed or incomplete cutter fails the test.

The programs topic had two straight-extrusion examples. A third, `turned_knob`,
revolves a line/cubic/arc profile into one exact solid. Its native volume matches
an independent integral of the authored profile (`parametric-programs`).

Not changed: Studio still draws legacy `extrude`/`sketch_cut` as hard
surfaces for every profile. Their closed meshes share vertices across the cap
rim, so smooth curved walls would need crease-split normals and a Studio GPU
check, not run here. Legacy sampling density is unchanged because it defines
existing mesh-authority volumes. The curved-design effect of these changes
remains unmeasured. It needs the matched live comparison above, plus an
open-ended curved benchmark case with machine checks (valid B-rep, curved
operations, curved face counts) and a separate human review.

This follow-up was verified on a local MinGW/OCCT 8.0.1 test build of the
source, without Studio. 34 CTest suites passed, including `native-solid`,
`snapshot-032d`, `media-044b`, `parametric-sketch`, `sketch-entities`,
`parametric-programs`, `onboarding-current-075d`, `assistant-connector`,
`associative-drawings`, `modeling-workflow`, `bend-workspace` and
`exploded-view-048b`. With `notched_tube` removed from the hard-surface set,
`snapshot-032d` failed on the notched-tube pixel check; with a guide hole
lacking `depth`, `parametric-sketch` failed on the cutter fields. A build
without OCCT passed `parametric-sketch`, `snapshot-032d`,
`onboarding-current-075d` and `media-044b`. That build exercises the same
sketch through legacy `extrude`. No installer, Studio capture or live provider
run was produced.

## Local verification

Six existing suites passed: `onboarding-current-075d`, `assistant-connector`,
`assistant-acp`, `assistant-claude-code`, `agent-workflow-033d` and
`specialized-component-patterns`. This covers build, guide refresh, provider
contracts and existing authoring flows, not live design quality. Receipt:
`out/release-068B/dev-build-20261108T232332438Z.json`.

The same six suites were rebuilt and passed again before source publication on
10 November 2026. Receipt:
`out/release-068B/dev-build-20261110T190649291Z.json`.

The first attempt passed five suites and failed an exact-text assertion for
preserving user constraints. The shared policy now retains that existing
sentence verbatim; no test expectation was weakened. The failed receipt is
`out/release-068B/dev-build-20261108T232207003Z.json`.

Only test targets were built for this follow-up. The installed Studio, release
identity and user documents were not changed; no new installer was produced.
Publishing this source for review does not qualify or distribute a new runtime.
