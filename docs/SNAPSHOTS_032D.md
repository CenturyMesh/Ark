# 032D — authored software snapshot appearance

**D.04/A.04 — Verificado no escopo.** Software `snapshot` resolves each feature's named
material and partial appearance override. Validated semantic face overrides map
through the producer's `face_spans` to the displayed triangles. Occurrences reuse
their definition's appearance after applying their own world placement.

Pixels include authored sRGB base color, opacity and emission color/intensity.
Emission has bounded display bloom and is sampled steadily; this does not render
the time evolution of pulse or blink effects. Opaque geometry writes depth first,
then transparent triangles are sorted back to front. Intersecting transparent
surfaces can have sorting artifacts. This renderer does not reproduce Studio's
PBR/metallic/roughness response. 044B adds triplanar image textures, procedural
pattern colors and surface stamps (GIF/video frames at `media_time_s`); plane
stamps and bump relief remain Studio-only. See [044B](MEDIA_TEXTURES_044B.md).

The evidence receipt identifies software capture, authored appearance scope,
steady emission and sorted transparency; since 044B `textures_and_stamps` is true
and `appearance_surfaces` lists the drawn textures, patterns, stamps and media
status. Geometry
remains `display_mesh` evidence; snapshot pixels never certify exact geometry or
manufacturing suitability. Existing unstyled rendering callers retain their
neutral display API.

Lighting interpolates the display mesh's vertex normals, as Studio does for exact
bodies. Previously every triangle was lit with its own normal, so curved display
meshes showed facet bands that are not in the B-rep. A corner normal more than
30 degrees from its triangle keeps the triangle normal, planar triangles keep
their flat pixels and back-face culling stays geometric. Bodies that Studio
draws as hard surfaces stay faceted: legacy `extrude` and `sketch_cut`, whose
sampled curve walls are mesh facets, and `notched_tube`, a welded planar mesh
whose averaged normals would bend its flat faces. The receipt states this as
`shading:"smooth_vertex_normals_except_legacy_extrude_sketch_cut_notched_tube"`;
`appearance_scope` lists only textures, patterns and stamps and no longer
describes lighting. Shading is display only; it changes no geometry or authority.

Synthetic verification covers authored material inheritance and feature
overrides, colored pixels, emission, transparency and material/triangle mismatch.
Integrated/native face checks passed, and front/isometric captures were visually
inspected for authored colors and emitted light. Results and limits are recorded
in [032D](iterations/032D.md).
