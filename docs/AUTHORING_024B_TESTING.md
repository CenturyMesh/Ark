# Testing 024B

Use an isolated test copy of a project. Do not merge the draft until the user
has reviewed the runtime. The local preview is separate from the build output:

```powershell
& 'C:\Sw_templates\Ark-024B\out\test-preview\024B-test1\ark-studio.exe'
```

Its identity is **024B / 0.9.25**. See [the verification receipt](verification-024B.json)
for binary hashes, actual tests and remaining boundaries. The preview includes
the native worker, runtime dependencies and dependency notices.

1. Start a single Assistant operation, then type a correction while it is busy.
   Use **Queue**. Attach another PNG/JPEG to that correction and check the
   selected image intent. The next provider request should see that content;
   an old queued write must reject. Repeat in team Focused and Parallel modes.
2. Select a native face. In **All tools → Stamps and face regions**, choose
   **Stamp on selected face**. Enter a PNG/JPEG path, dimensions and projector
   axes. Check alpha, orientation, depth bounds and clipping to that face.
   Save, close and reopen after moving the original image file: pixels must
   remain embedded. Change the width/height expressions and use Undo.
3. Use **Stamp on sketch plane** with an active plane. Change its offset and
   verify the image follows. **Remove stamp** removes the projection.
4. Create a connected sketch above a planar/cylindrical face. Select that face
   and choose **Split line** with the correct projection direction. Inspect the
   new region references, then use **Face / region appearance** on one region.
   Other regions keep their own color; the body and volume remain intact.
5. In **Sketch planes and sections**, try **Sketch to plane**, **Offset plane**,
   **Section from plane** and **Section from sketch line**. Change a line endpoint
   parameter and verify that its section plane and exact cut follow. Contour
   creates a curve; positive/negative/both create retained-side cut features.
6. Ask an agent to load `agent_onboard` topics `stamps` and `planes`. Verify it
   inspects actual face names/digests before applying an image or color. A logo
   generation step requires an available client image-generation capability;
   importing an existing image alone must not be reported as generation.

Use `snapshot{view:"viewport"}` to inspect Studio rendering. Software snapshots
and the optional path tracer have a different appearance scope. General curved
UV wrapping, exact engraving from pixels and arbitrary split projections remain
unsupported. Drawings are authored only on explicit user request.
