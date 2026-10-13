# Snapshot orientation and live viewport evidence

The `snapshot` tool returns display pixels with the document revision and
selected feature/occurrence identity. The pixels never select production
geometry. Exact inspection remains `native_inspect` and the source B-rep.

Software-rendered `iso`, `front`, `right` and `top` presets use Z as model up.
`front` views the target from -Y, `right` from +X, and `top` from +Z. The iso
view is from +X/-Y/+Z. `visual_evidence.view_state` records the actual target,
target-to-eye direction, up vector, distance and field of view.

For a view chosen by the caller, pass `view:"custom"` (or omit `view`) and both
camera vectors:

```json
{
  "expected_revision": 12,
  "id": 7,
  "view": "custom",
  "camera": {
    "direction": [0, -1, 0],
    "up": [0, 0, 1],
    "target_mm": [0, 0, 0],
    "distance_mm": 250,
    "fov_deg": 35
  }
}
```

`direction` points **from the target toward the camera**. `up` is the screen
vertical hint. Both must be finite, nonzero and nonparallel. Target, distance
and field of view are optional; a selected body's evaluated display bounds
provide the default target/distance. `distance_mm` must be positive and
`fov_deg` must be in 5..120. A custom camera cannot be combined with a preset
view. Invalid requests and stale revisions reject without pixels or document
changes. Prepared candidate captures use the same camera contract and remain
uncommitted.

Studio's live `view:"viewport"` captures the actual current viewport. It is
read-only and reports the camera/render state and selected object separately.
The viewport readback binds the default framebuffer explicitly and reports
render versus pixel-readback errors with the OpenGL code. Software snapshots
still use the bounded clay display renderer: textures/material appearance,
occluded details and manufacturing evidence require separate inspection.
