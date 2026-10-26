# 057B — View cube and display controls

Milestones: **B.01** (CAD viewport/navigation), **A.04** (observable validation).
Status: **Verificado no escopo** in the local Windows Release build. Native
input checks and synthetic-fixture captures were verified on an RTX 4070 Ti;
broader hardware and CAD milestones remain **Parcial**.

The orientation cube now has broad square faces and a shallow chamfer, with the
same 26 retained picking surfaces (six faces, twelve edges, eight corners).
Words are projected onto their actual face planes, using the existing retained
Segoe UI font atlas. The contract remains +X Right, -X Left, +Y Top, -Y Bottom,
+Z Front and -Z Back. Camera roll also rolls the face labels. Neutral matte
surfaces, restrained contours and blue hover/selection replace the larger bevels
and amber highlight. Home has a vector house icon and retains fit/isometric view.

One grouped viewport bar provides Shaded, Shaded with edges, Wireframe, planes,
sketches, grid, projection, Fit, selected-source drawing and More. Mode selection
is visible beside independent overlay toggles. The group title identifies the
current display style; hover explains each action. The bar uses the existing
OpenGL vector icon system and moves beside the Cartesian triad when space allows.
The cube leaves room for it in short viewports. The separate top-right style
dropdown is removed; More opens its existing settings and named-view menu.
The navigation toggle expands Orbit, Pan, Zoom and ambient occlusion as an
optional group in this same bar. Fit, edges and projection are not duplicated.
At constrained widths, direct styles and More remain; More explicitly includes
overlays, navigation, named views and selected-source drawing. A reference-mode
hint banner collapses only where it would cover the display bar; the ribbon's
selection mode and all selection behavior remain available.
Grid scale captions follow the same intersection rule. Contextual sheet/tube
and suspension playback uses a 44-pixel display bar with the same 32-pixel hit
targets and tooltips, omitting group captions so the full bar clears playback.
Descriptors and local hit-zone storage use fixed arrays rather than allocating
collections on each frame. This does not claim the complete draw path is free
of allocations or demonstrate a frame-time reduction.

Explicit raster style selection turns off path tracing and saves the style.
Projection keeps its existing persisted preference. Display/navigation controls
and neutral toolbar padding preserve inertial playback. Opening a drawing remains
explicit and retains the selected part/container source contract. CAD geometry,
document revision, file format, mesh authority and production tolerances are
unchanged.

## Observed validation and source review

- `git diff --check`: passed for the integrated source.
- `studio-navigation-036a`: passed 208 real face/edge/corner clicks across four
  physical sizes and two rolls, 48 cardinal text pixel checks, antialias coverage,
  Home/orbit/padding and retained allocations. Independent camera-normal
  agreement is checked for every clicked facet so a self-consistent but wrong
  named view cannot pass. At 1180x800 and 1600x1000, actual style clicks passed
  with Assistant and navigation controls both open, requiring a cube at least 96 pixels
  across and disjoint control rectangles. A real occurrence of the public startup
  fixture keeps Appearance, Opacity and Move visible; all three must remain
  disjoint from the bar. Orbit/Pan/Zoom/AO passed through the same Win32 path.
  Toolbar, cube and triad share placement geometry; wide, short viewports retain
  readable face labels.
- `studio-viewport-dock-ui` and compact counterpart passed actual Win32 clicks for
  all raster styles, projection, overlays and Fit; settings reload; neutral
  padding; bounds and triad separation; selected part/container drawings,
  explicit creation and Assistant layout. Each style and More hover were captured.
- Overview, cardinal/diagonal/hover, compact-layout and full viewport captures
  were inspected. The compact controls and selected-style captures were also
  independently reviewed by the viewport agent.
  Native text is reused; no generated image asset or private drawing fixture is
  introduced.
- The final source `eb1856e` passed 14 selected tests in 28.47 seconds, including
  action icons, inertial UI, drawings, render and picking. An intermediate build
  also passed asynchronous exact-work Cancel priority. Context and suspension
  self-tests passed with the full display bar checked against the playback panel,
  rather than only checking one navigation button. The test and capture receipts
  are recorded in the [iteration evidence](iterations/057B.md).

This change does not claim faster frame time: retained geometry/font/target
allocation assertions remain, and paint timings are receipts rather than a
before/after benchmark. Supersampling still uses the existing 3x target and
fallback; no graphics backend or hardware qualification is added.

A preexisting limitation remains outside this change: at a 160-pixel-high
suspension viewport, its legacy full-width physical panel can intersect the cube
and Appearance panel. Source comparison confirmed the intersection on the 056C
base. The display bar now clears playback, but this is not a claim that every
legacy contextual panel has been redesigned or qualified at that height.
