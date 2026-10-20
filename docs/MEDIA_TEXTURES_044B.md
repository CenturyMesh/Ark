# 044B — procedural textures, animated GIF/video media and textured snapshots

**D.04 — Parcial (local scope verified); B.01 — Parcial.** Appearance stays
display authority only. B-rep, STEP and manufacturing data never read pixels,
patterns or media.

## Procedural patterns

A material or feature/face appearance can carry a pattern without any image:

```json
{"op":"material.set","name":"Twill fabric","density":0.4,
 "appearance":{"base_color":[0.18,0.2,0.24],"roughness":0.85,
   "pattern":"twill","pattern_scale_mm":2.5,"pattern_angle_deg":45,
   "pattern_contrast":0.7,"pattern_color":[0.08,0.08,0.1]}}
```

| Field | Range | Default |
|---|---|---|
| `pattern` | `twill`, `weave`, `carbon`, `leather`, `brushed`, `knurl`, `perforated`, `none` | none |
| `pattern_scale_mm` | 0.05..1000, the pattern period | 4 |
| `pattern_angle_deg` | -360..360 | 0 |
| `pattern_contrast` | 0..1 | 0.35 |
| `pattern_color` | sRGB `[r,g,b]` or `null` | base color × 0.45 (× 0.08 for perforated holes) |

The pattern is a scalar field of part-local millimetres, blended triplanar by the
local normal like image textures, so it follows assembly copies without UVs.
Studio's shader mixes the secondary color, adjusts roughness (fabrics/leather
vary, carbon tows are smoother, knurl grooves and holes rougher, brushed keeps
the authored value) and adds a screen-space bump relief of
`(1-field)·contrast·period·0.06` mm. The C++ field
(`appearancePatternField044B`) and the GLSL use the same formulas; the software
snapshot draws the color field without bump or roughness. Patterns combine with
an image texture (the texture multiplies first).

Pattern fields are written only when set, so existing documents keep their
bytes. Any pattern field saves document source **version 47**. Appearance-only
edits (`material.set`, `feature.edit{appearance}`, `face_appearance.set`) report
`geometry_recomputed:false`. The Studio **Material** dialog adds Procedural
pattern, period, angle and contrast fields. The optional path tracer reports a
raster fallback for patterned looks.

`texture_asset` textures now tile: embedded PNG/JPEG assets used as material
textures are sampled with repeat wrapping (stamps still clip to their rectangle).

## Animated media assets

Media share `image_assets` (capacity 64) with the existing images:

```json
{"op":"image_asset.set","asset":{"name":"Display loop","mime_type":"image/gif","data_base64":"R0lGODlh..."}}
{"op":"image_asset.set","name":"Display clip","path":"media/display.mp4","embed":false}
{"op":"stamp.set","name":"Centre display","asset":"Display clip","feature":"Display","faces":["z_max"],
 "source_topology":"<observed brep_sha256>","origin":[0,0,2],"u_axis":[1,0,0],"v_axis":[0,1,0],
 "width":180,"height":70,"loop":true,"speed":1,"autoplay":true,"start_s":0}
```

- **GIF** (`image/gif`): embedded bytes, at most 4 MiB, 2048 px per side and 512
  frames. Frames are composited (disposal, transparency) by the bundled stb
  decoder on every platform; delays of 10 ms or less play as 100 ms, as browsers
  do. The asset records `width`, `height`, `frames` and `duration_s`, and its
  SHA-256 must match the bytes.
- **Video** (`video/mp4`, H.264 in MP4/M4V): stored by `path` relative to the
  `.ark` plus the SHA-256 of the file bytes. `embed:true` also stores the bytes
  (at most 2 MiB, still within the 4 MiB document cap); embedded bytes play from
  a private temporary file when the file beside the document is absent. An
  absolute stored path rejects. The Studio Stamp dialog accepts `.gif`/`.mp4`
  and stores a video relative to the saved document; agents pass a path that
  resolves from the working directory and is relative to the document.
- **Playback** on a stamp: `loop` (default true), `speed` (0, 16], `autoplay`
  (default true; false holds `start_s`), `start_s` 0..86400. Position is
  `start_s + elapsed·speed`, wrapped over the whole clip or held at the end.
  These fields are written only when non-default. A GIF or video used as a
  material `texture_asset` plays with the defaults.

Any media asset or non-default playback field saves version **47**.

## Studio playback

- GIF frames decode with `std::async`; MP4 frames decode on a background thread
  (`VideoStream044B`, Media Foundation source reader with RGB32 conversion,
  frames at most 1024 px by an integer box filter) into a bounded queue of four
  frames. The GL texture is updated only when the displayed frame changes.
- The media clock advances only while media are drawn and the window is not
  minimized (at most 0.25 s per tick, so hidden periods do not jump ahead).
  Studio's main loop already redraws continuously; no extra redraw source is
  added.
- A missing file, changed bytes (`hash_mismatch`), Media Foundation absent
  (for example a Windows N edition without the Media Feature Pack) or an
  undecodable stream reports one status message and draws nothing for that
  stamp. Media Foundation is loaded at run time; only the static `mfuuid` GUID
  library is linked, so ARK starts without it. Other platforms report
  `VIDEO_UNSUPPORTED` from the same API.

## Software snapshot

`snapshot` now draws triplanar image textures (repeat per 100 mm, bilinear),
procedural pattern colors and surface stamps (bilinear, clipped to the
projector rectangle, half depth and front facing, alpha × opacity). Plane
stamps (`feature:0`) are not drawn. `media_time_s` (0..86400, default 0) selects
the GIF/video frame through the same playback position. `visual_evidence`
reports `textures_and_stamps:true`, `media_time_s`, `appearance_scope` and
`appearance_surfaces` (`textured_surfaces`, `patterned_surfaces`,
`stamps_drawn`, `media` rows with status, frame/position or error,
`missing_textures`, `stamps_skipped`). Lighting stays the flat diffuse model of
[032D](SNAPSHOTS_032D.md); output is deterministic for a given document and
time.

## Local evidence (044B branch, Windows 11, MSVC 14.44)

- `ark-media-044b-tests` (CTest `media-044b`): pattern field range and
  determinism; pattern patch/JSON round-trip and rejections; material and
  feature pattern edits with `geometry_recomputed:false`, version 47 byte-exact
  round-trip, version 46 rejection, restoring the earlier version after removal;
  software snapshot pixels change with a checker texture (mean |Δ| 23.3) and a
  knurl pattern (13.9) and are deterministic; snapshot tool evidence counts;
  synthetic three-frame GIF decoded with exact colors and 0.1/0.2/0.3 s delays,
  loop/hold frame selection, truncated and non-GIF rejection, GIF asset version
  47 round-trip, hash mismatch and atomic corrupt-import rejection; stamp
  playback fields and positions; a 12-frame 160×96 H.264 clip written with
  `IMFSinkWriter` and decoded back (size, ~1 s duration, colors within 40 and
  vertical orientation at four times, bounded downscale), a looping stream that
  delivered 30 increasing frames to 2.42 s, video asset by path and by
  document-relative path, embedded bytes, version 47 round-trip, and explicit
  `hash_mismatch`/`missing` statuses; a stamped GIF screen whose snapshot center
  pixel is red, green and blue at 0, 0.15 and 0.45 s and the tool report selects
  frame 2 at 0.45 s.
- `ark-studio --self-test-media-044b-ui` (CTest `studio-media-044b-ui`, real
  OpenGL window): framebuffer samples show the GIF stamp red/green/blue at 0,
  0.3 and 0.5 s; all seven patterns raise the luminance variance of a plain face
  (0.14) to 418–1658; a synthetic H.264 clip stamp shows red at 0.1 s and green
  at 0.6 s; with the live clock the displayed frame changed (3 colors in 1.6 s);
  minimizing held the media clock. Captures were inspected visually.

Not verified: GPU/driver variety, long or high-resolution videos, audio (never
decoded), HDR/10-bit video, H.265 or other codecs, color calibration, frame-exact
synchronization between several stamps, playback on non-Windows platforms, and
any printing or physical display qualification.
