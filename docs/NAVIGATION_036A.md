# Faceted navigation widget

This development scope addresses **A.01/A.04**. Status: **Verificado no escopo**
for the native build, interaction probes and capture inspection below.
The integration owner records the distributed iteration and runtime separately.

The viewport compass has six named cardinal faces, twelve broad edge faces and
eight corner faces. Each of the 26 visible facets owns its rendered triangles,
hover state and ray-hit direction. Cardinal clicks preserve the existing world
axis mappings; intermediate clicks select their two-axis or three-axis
orthographic direction. Clicking resets camera roll. Dragging still orbits and
Home retains the existing perspective view and framing behavior.

Geometry and seam buffers are uploaded once. A retained target renders the
widget at three times its physical pixel extent and resolves nine samples per
output pixel. Six Segoe UI Semibold word tiles are rasterized once at 64 pixels
and drawn into the same target. Labels remain screen aligned under camera roll,
and shrink or disappear when their rectangle would cross the associated main
face. Hover is amber, the aligned direction is cyan, and seams distinguish the
intermediate facets. Padding consumes pointer clicks instead of selecting a
body or reference behind the navigation control.

No model geometry, CAD coordinates, document format or application identity is
changed by this work.

## Verification

Run the native Studio executable with its existing navigation test flag:

```powershell
ark-studio.exe --self-test-navigation-ui --shot navigation-036a.png
```

The test checks:

- All 26 directions through actual Win32 mouse dispatch, with two roll states
  and physical widget extents of 96, 136, 204 and 272 pixels (208 snap checks).
- Cardinal glyph ink and partial coverage in actual GL readback, blended
  silhouette pixels and distinct facet hover pixels.
- One-time geometry/font construction and unchanged target allocation counts
  during twelve orbit frames at each size.
- Home, drag orbit, padding capture, unchanged document revision/epoch and
  unchanged model/reference selection.
- Existing navigation layout and RGB triad pixels at 1180 x 800 and 1600 x 1000.

The screenshot prefix produces full-window images, cardinal/edge/corner detail
images, overview/hover images and a `.navigation-test.log` receipt. These are
actual viewport pixels. Inspect the compact and nominal widget captures as
well as the larger captures before accepting the visual change.

Physical pixel-size probes exercise the raster path at compact, nominal, 150%
and 200% extents. They do not establish a Windows monitor-DPI transition test,
keyboard accessibility, driver-independent appearance or manufacturing
qualification. Broader A.01/A.04 milestones retain their existing scope.

Local Windows verification on 17 October 2026:

- Release `ark-studio` built with the pinned native configuration and two build
  workers. `--self-test-navigation-ui` and `--self-test-projection-ui` returned 0
  using isolated settings under `out/navigation-verification`.
- The navigation receipt reports 208 mouse snap/roll/size checks, 48 cardinal
  text pixel checks and 1625 blended outline pixels. Geometry and font were
  built once; the render target allocation count stayed unchanged throughout
  each set of twelve orbit frames. Home, orbit, padding and unchanged document
  revision/epoch checks passed.
- Actual GL captures at both window sizes and widget extents 96, 136, 204 and
  272 were inspected. Main words, edge/corner facets, silhouette and amber hover
  were checked. At compact extent, oblique labels may be omitted when a legible
  word cannot remain inside its own face. The grid readout moves clear of the
  widget when the assistant dock leaves a short viewport.
- Retained widget paint plus GPU finish, excluding screenshot readback, was
  sampled over twelve orbit frames per extent. Median/max milliseconds were
  0.1194/0.1871 at 96 px, 0.1172/0.1857 at 136 px, 0.1295/0.1873 at 204 px and
  0.0998/0.3245 at 272 px. These are local warm samples, with no baseline speedup,
  physical input latency, compositor or monitor presentation claim.
- The first attempted run rejected a disappearing cardinal word at 96 px.
  Broadening the six main faces preserved the minimum font size under roll;
  the complete repeated run then passed. The failed receipt remains in the
  local verification output alongside the successful captures.

The confirmed capture prefix is
`out/navigation-verification/navigation-confirmed.png`; its
`.navigation-test.log` retains the counts and timings. Screenshot/build output
is local evidence, not part of the source change or a distributed release.
