# 056C Inertial Simulation Studio

Milestones: **C.04 — Parcial**, **A.04 — Verificado no escopo** for the local
Studio transport/control checks described below. Physics accuracy and the
integrated GPU runtime require the engine verification record; the synthetic
UI fixture is explicitly not physics evidence.

## Delivered controls

`Inspect > Simulation` and the `Simulation` command category open a docked
study. Study selects all visible or selected occurrences, explicit GPU or CPU
reference, duration, integration step, output rate and solver iterations.
Environment sets world XYZ gravity, optional ground Z, contact coefficients,
body contacts and preservation of supported CAD mates. Choosing detached bodies
is explicit. Body loads select a named feature/node/copy occurrence and set CAD
fixing, dynamic/fixed override, constant force/torque, and initial linear/angular
velocity. SI units are shown; ground and display positions use millimetres.

Run submits `physics_inspect` to the existing background exact-read scheduler.
The scheduler captures an immutable private session on the UI thread; calculation
and cooperative cancellation run off that thread. Run is unavailable when this
service is disabled, with an explicit error. It never falls back to a blocking
UI call or silently changes the solver. One study job is retained until its
completion/cancellation; editing study inputs cancels that job before another
can start. Closing Studio retains the scheduler's existing shutdown/join path.

Play/pause, timeline drag, previous/next frame, playback speed and Reset pose
operate on display matrices only. Space toggles play, arrows step, Home/End
seek and Esc restores authored CAD. Fit remains available while displaying the
result. Other authoring actions reset the preview. Other motion and drawing
panels are mutually exclusive with this dock. Closing the dock restores CAD
pose; it does not discard a background computation.

Results are bound to the captured revision, document epoch and request
generation. Unexpected revision/occurrence identities, duplicate bodies,
incomplete or unordered frame times, nonfinite and nonrigid poses are rejected.
An edit, document replacement or late callback cannot publish an old playback.
Loads clear on document replacement. Results and overrides are session-local;
this UI does not save a study, change `.ark` schema, call `doc_apply` for poses,
or alter B-rep authority. Evidence is a read-only formatted result with the
request, source SHA256, diagnostics, initial/final states and solver limits.

## Scope and limits

The engine contract requires declared material density, exact CAD mass and
inertia, and closed convex collision meshes. It currently supports unlimited
revolute CAD mates; unsupported mates are explicit failures. The UI preserves
these contracts, including 128 bodies, 14,400 integration steps and 32,768
returned body states. Ground and body contacts are discrete; this is not FEA,
deformation, CCD or manufacturing qualification. CPU reference is an explicit
comparison mode. CPU preparation and GPU solving remain separate stages.

## Verification

`studio-inertial-ui` drives the real Inspect ribbon, dock buttons, timeline and
native modal controls on an isolated synthetic CAD assembly with declared
density. Its deferred transport supplies labelled synthetic frames and checks:

- Apply, Cancel, invalid settings, selected/all occurrence scope and body loads.
- Play, pause, seek, stepping, speed and exact restoration of authored poses.
- Source and revision remain unchanged during simulation controls/playback.
- Cancellation, old-generation callbacks, malformed nonrigid poses, document
  replacement and revision changes cannot install stale results.
- OpenGL renders and PNG captures of study, environment, loads, evidence,
  running, final, reset and stale states.

The initial 1440x900 CTest passed in 3.75 seconds. Inspection identified a clipped
angular-unit label and small dock text; these were corrected and a native vector
icon was added. The subsequent four tests passed: inertial UI (3.18 s), action
icons (3.21 s), baked motions (3.75 s), and exploded views (1.95 s). The same
inertial fixture at 1180x800 exited 0. Captures at both sizes were inspected.
Evidence formatting also uses native CRLF line breaks and a result summary;
status text wraps at spaces. No user document is loaded by the fixture, and no
external Studio process is closed by it.

`--self-test-inertial-engine-ui` performs the same control/playback/source checks
using the production private-session scheduler first with CPU reference, then
with GPU. It has no synthetic transport. Run this on the integrated engine build
with separate `--settings-file`, `--shot` and `--size 1440 900` arguments. Each
solver has a 90-second test wait while the UI pumps and renders. The receipt
retains both actual solver reports. This branch's UI-only runtime cannot prove
engine integration; the integrated delivery must run this flag before claiming it.

Artifacts are under `out/quality-native/ui-checks`; build and test logs are
`out/inertial-*.log`. These local artifacts are not distribution content.
