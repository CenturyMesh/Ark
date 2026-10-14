# Plane and section authoring — 024B

024B uses the existing editable construction-plane, sketch and native feature
contracts to expose creation, offsets, sketch reassignment and exact sections.
This is bounded B.01/B.02/B.03 authoring scope. A section contour and a material
cut have different concrete outputs; the selected mode remains explicit.

`plane_authoring` prepares or applies one atomic recipe at the observed
`expected_revision`. Its actions are:

| Action | Required inputs | Result |
|---|---|---|
| `create_plane` | `name`; optional `parent`, `p` | Saved parent-relative plane with editable `ox/oy/oz` millimetres and `rx/ry/rz` degrees. |
| `offset_plane` | `name`, `plane`, `offset` | Associative signed offset along the source plane's normal; the numeric expression is retained. |
| `sketch_to_plane` | `sketch`, `plane` | Reassigns the saved plane while retaining all local authored coordinates, expressions, entities, constraints and corner treatments. |
| `section` | `id`, `name`, `plane`; optional `mode` | Exact contour or an explicitly retained material half-space from a saved plane. |
| `section_from_line` | `id`, `name`, `sketch`, `entity`; optional `plane_name`, `mode` | Associates a perpendicular section plane with one authored straight sketch line and then creates the requested contour or cut. |

New output names must be unused. A recipe cannot silently overwrite a user's
plane or feature. `preview:true` exposes the proposed commands without applying
them; the actual document write still passes the ordinary atomic revision and
geometry validation. Missing resources, invalid expressions, dependent geometry
failures and stale revisions reject without publishing partial datums or solids.

`sketch_to_plane` preserves authored XY coordinates in the new plane. It is a
change to the sketch's 3D placement, not a projection that keeps every source
point at its previous world position. Referenced sketches and dependent features
must pass recomputation after that edit.

Section modes are `contour` (default), `positive`, `negative` and `both`.
`contour` creates an editable `intersection_curve` feature from the retained
exact solid and plane, using the existing one-connected-wire domain with at
most 128 edges. Disconnected sections reject rather than drop loops. It does
not remove material. The signed modes create `native_solid` `split_plane`
features. Positive and negative retain the requested side of the plane normal;
`both` retains separate exact solids in the result. The original source body
remains in feature history in every mode.

For `section_from_line`, the saved recipe creates an `axis_points` datum from
the named sketch line's start/end references and an `angle_plane` datum obtained
by rotating the sketch plane 90 degrees around that axis. The generated plane
passes through the directed line and is perpendicular to its source plane.
This retains the sketch's expression and plane dependencies: changing a line
endpoint updates the section plane and its downstream exact result.

The cut uses an **infinite plane**. The finite line identifies its orientation
and position; it does not define a finite-length kerf or a curved cutting tool.
A curved sketch boundary instead needs the appropriate authored cut feature.
Face partition for colors/textures uses [split lines](SPLIT_LINES_024B.md).
These source recipes reuse existing document-format rules; the application
release identity itself does not increment the document format.

The local `plane-sections-024b` test passed **43 checks**, covering retained
sketch source, saved plane expressions,
associative offsets, exact section lengths, line-derived plane orientation,
signed retained volume, endpoint-driven recomputation, source reload and
explicit rejection. It is local CAD evidence, without fabrication qualification.

The Studio selftest exercises the rendered tool-gallery rows and actual native
dialogs for sketch reassignment, offset, saved-plane section and line-derived
material cut, with synthetic source preservation, cancellation and parameter
recomputation. Its minimized-size check sends a synthetic `WM_SIZE /
SIZE_MINIMIZED` notification and verifies that the prior valid viewport
dimensions survive; it does not reproduce or diagnose a user's earlier log.
Screenshot results and supported window sizes are recorded in the 024B delivery
evidence after visual inspection.
