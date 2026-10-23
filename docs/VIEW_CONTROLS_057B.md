# 057B — View cube and display controls

Milestones: **B.01** (CAD viewport/navigation), **A.04** (observable validation).
Status at source handoff: **Em curso**; native integrated build, real input tests
and visual inspection are required before claiming verification.

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

Explicit raster style selection turns off path tracing and saves the style.
Projection keeps its existing persisted preference. Display/navigation controls
and neutral toolbar padding preserve inertial playback. Opening a drawing remains
explicit and retains the selected part/container source contract. CAD geometry,
document revision, file format, mesh authority and production tolerances are
unchanged.

## Validation plan and source review

- `git diff --check`: passed at source handoff.
- `studio-navigation-036a`: retain 208 real face/edge/corner clicks across four
  physical sizes and two rolls, 48 cardinal text pixel checks, antialias coverage,
  Home/orbit/padding and retained allocations. Add independent camera-normal
  agreement for every clicked facet so a self-consistent but wrong named view
  cannot pass. At 1180x800 and 1600x1000, also exercise actual style clicks with
  Assistant and navigation controls both open; require a cube at least 96 pixels
  across and disjoint control rectangles. Toolbar, cube and triad share placement
  geometry; wide, short viewports retain readable face labels.
- `studio-viewport-dock-ui` and compact counterpart: actual Win32 clicks for
  all raster styles, projection, overlays and Fit; settings reload; neutral
  padding; bounds and triad separation; selected part/container drawings,
  explicit creation and Assistant layout. Capture each style and More hover.
- Inspect generated overview, cardinal/diagonal/hover and full viewport captures.
  Native text is reused; no generated image asset or private drawing fixture is
  introduced.
- Integrated regressions should include action icons, inertial UI, drawings and
  asynchronous exact-work Cancel priority, because these share viewport layers.

This change does not claim faster frame time: retained geometry/font/target
allocation assertions remain, and paint timings are receipts rather than a
before/after benchmark. Supersampling still uses the existing 3x target and
fallback; no graphics backend or hardware qualification is added.
