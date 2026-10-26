# Agent image and animation tools

Milestones **A.02 / B.04 / D.02 / E.01**. Display media never supplies CAD
geometry, manufacturing qualification, sensor measurements or hardware triggers.
The existing `image_asset` tool exposes the following workflow without increasing
the tool catalog count. Load `agent_onboard` topic `stamps` first.

## Discover and generate

`image_asset{action:"capabilities"}` reports local composition, text and video
support and whether image API credentials are configured. Credential presence
does not prove model access or a successful provider request.

`action:"generate"` requires `model`, `prompt`, a new `.png` output `path`, and
`expected_revision`. Optional `size` is `1024x1024`, `1536x1024`, `1024x1536` or
`auto`; `quality` is `low`, `medium`, `high` or `auto`. The request goes to the
[OpenAI Images API](https://developers.openai.com/api/reference/resources/images/methods/generate)
and returns actual decoded PNG bytes, dimensions, SHA-256 and provider usage when
reported. An unavailable key or rejected response produces an explicit error.

Configure the key through **Models/account → Image generation / OpenAI API key**
or `OPENAI_API_KEY`. Studio stores it through the existing per-user encrypted
credential store, scoped to the OpenAI provider and HTTPS host. The dialog keeps
the current assistant model and ChatGPT login. It shares the saved OpenAI API key
with the OpenAI API connector; it does not convert ChatGPT plan credentials into
an Images API key. No key is accepted as a model tool argument or written to an
artifact, document, log or settings JSON.

An external client's image-generation skill may also supply real local PNG/JPEG
files. Such skills are not automatically available inside the Studio agent.

## Compose locally

`image_asset{action:"compose",expected_revision,path,format,width,height,fps,
duration_s,background,layers}` writes a new PNG, GIF or MP4. It works without an
image API key. Inputs are explicit layers, not executable scripts:

- `image`: exactly one PNG/JPEG `asset` name or local `path`.
- `rect`, `ellipse`, `line`: pixel geometry, `color` as `#RRGGBB`, optional
  `outline` and `line_width`.
- `text`: Unicode `text`, pixel `font_size`; `{frame}` and `{percent}` expand
  from the clip clock. Native text currently requires Windows.
- Layer `x`, `y`, `width`, `height`, `x2`, `y2`, `opacity`, `from_s`, `to_s` and
  `line_width` accept numbers or expressions over `t` (seconds), `u`, `frame`,
  `duration`, output `width`/`height`, and document parameters. The media clock
  names override same-named document parameters. `from_s` is inclusive and
  `to_s` exclusive. Text and image layers remain raster display content.

The default is 512×288 at 20 fps. Limits are 16..1024 pixels per side, up to
60 seconds, 512 frames, 64 layers, 256 MiB of decoded output frames and 64 MiB of
decoded source images. GIF allows 1..50 fps and at most 4 MiB, with a shared
adaptive palette and delta rectangles. MP4 uses the existing Windows H.264
encoder and requires even dimensions. Unsupported encoders are reported.

The receipt states actual frame count and duration: non-integral duration×fps
rounds up to a frame, and GIF timing is rounded to centiseconds. It includes
SHA-256 and decoded readback evidence. PNG/GIF readback is exact in their encoded
pixel representation; MP4 is lossy and its first decoded frame is checked for
dimensions. Neither is a real-time playback-performance claim.

The tools stage and validate output before exclusive publication. Existing
destinations are never replaced. Publication requires filesystem hard-link
support; an unsupported filesystem or racing destination rejects explicitly.
Cancellation before publication leaves no final artifact. Files do not change
the document or count as applied media.

## Import and apply

Use `image_asset{action:"import",name,path,expected_revision}` after generation.
GIFs and static images embed their bytes. Use `embed:true` for a portable MP4 up
to 2 MiB; larger clips must remain external and use the existing relative-path
contract. Project the asset with `stamp.set` using inspected face references and
the producer's `source_topology`. Generation never selects production faces.

## Bind a display preview to motion

In a **live Studio session**, call:

```json
{
  "action": "playback",
  "mode": "bind",
  "motion": "Named baked motion",
  "assets": ["Screen loop"],
  "leds": [{"feature": 42, "intensity": "max(0,1-abs(t-10.5)/0.15)"}],
  "expected_revision": 12
}
```

The named motion must have a current bake. GIF assets use the same discrete
baked-frame clock as the displayed assembly. Existing stamp playback mappings
still apply. LED targets must already have an authored emissive appearance;
their intensity expressions are validated as 0..1 at every baked frame and
replace the independent pulse/blink multiplier during the binding. Expressions
may also use compiled motion channel values. This scales appearance only.

Start the named motion in **Baked Motions**, or use `action:"playback"` with
`mode:"play"`, `"pause"`, `"stop"` or `"seek"` and the current
`expected_revision`; seek also requires `time_s`. Native flexible-cable
preparation returns `pending:true` with measured progress rather than pretending
playback has started. Play starts automatically when that preparation succeeds;
a pending seek must be requested again after preparation. Pause holds its media
frame, scrubbing uses the selected frame, and Stop returns media/cues to time zero. `mode:"inspect"`
reports the binding; `mode:"clear"` with the current revision removes it.
Bindings are **session-local**, are not saved in `.ark`, and become stale after
a revision or document change. Rebind explicitly after edits. MP4 remains
independent playback: frame-indexed MP4 synchronization is not claimed.
Standalone MCP rejects playback binding and instructs the client to connect to
Studio. No physical camera, sensor or LED hardware is triggered.
