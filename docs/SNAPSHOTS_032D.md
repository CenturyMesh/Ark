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

Synthetic verification covers authored material inheritance and feature
overrides, colored pixels, emission, transparency and material/triangle mismatch.
Integrated/native face checks passed, and front/isometric captures were visually
inspected for authored colors and emitted light. Results and limits are recorded
in [032D](iterations/032D.md).
