# Image authoring — 019B

This scope contributes to **B.01**, **B.03** and **D.02**: a real user-image
attachment reaches the ARK authoring assistant and the Studio team, with an
explicit design intent and editable-model instructions. It is not a certified
photogrammetry or automatic exact reconstruction engine.

In the Assistant composer, choose **Attach image**, select a PNG or JPEG, and
review its thumbnail. Select one of the two modes:

- **Inspire design** asks the agent to translate the image's shape language,
  curves and proportions into a coherent editable concept using ARK tools.
- **Image to CAD** asks the agent to recreate the visible form with editable
  sketches, parameters, features or an explicitly authored mesh body. It directs
  the agent to inspect the current document, compare actual model snapshots and
  silhouettes in matching views, and disclose scale and hidden-geometry assumptions.

For a usable drawing or silhouette, Image to CAD first guides the agent toward
coherent editable contours made from lines, arcs and splines, then dimension
scaling, datums/a parametric skeleton, and 3D extrude/sweep/loft or surface features.
The agent must assess the image type first. Perspective photos keep depth and
hidden surfaces provisional; shadow/background edges are not automatically CAD
contours. This staged guidance uses existing authoring tools and does not advertise
an automatic pixel-vectorization tool.

**Image to CAD** exposes three optional rough overall dimensions in millimetres:
**Width**, **Height** and **Depth**. Each supplied positive number guides its own
overall dimension. Each blank field is left to the model's judgment; leaving all
three blank also works. Values are estimates, not measurements extracted from
image pixels. Switching to Inspire design hides the fields and preserves their
values; switching back restores them. Attaching another image or removing the
pending attachment clears those estimates. Invalid, zero, negative, non-finite
or oversized numbers reject before sending; no zero/default dimensions are
silently inserted.

The image remains local until **Send**, **Start** or **Queue** is used. Attaching,
previewing, switching intent and cancelling a file picker do not contact a model.
Remove discards the pending attachment. A changed document, connector/account or
explicit object target invalidates the pending attachment and requires attaching
it again. Filename, mode and supplied estimates appear in the visible chat;
private directory paths and image bytes do not appear in that log.

The actual PNG/JPEG bytes are validated and fully decoded in memory before they
enter a user message. The limits are **4 MiB** encoded file size, **8192 pixels**
per side and **16 megapixels** decoded area. The thumbnail uses those decoded
pixels. Responses, compatible Chat Completions and native Anthropic Messages
requests retain the actual inline image payload; the ChatGPT plan request uses
the same Responses content. Individual model/provider image support remains a
connector capability: an explicitly unsupported capability rejects rather than
silently replacing the image with a filename or text. Provider rejections remain
explicit; the host does not promise image support for every model advertised by
a compatible endpoint.

Windows uses WIC for its normal decode/thumbnail path. Headless builds on other
platforms use the pinned portable stb_image PNG/JPEG memory decoder, with the
same file, dimension and pixel-area bounds and a 192 MiB limit on decoder
allocations. It validates the actual decoded bytes and produces a thumbnail;
portable provider/team tests run rather than being skipped. Windows offline
contracts also force that portable path with synthetic PNG and JPEG pixels.

The connector's serialized request limit also applies after base64 encoding and
including conversation/tool context. In particular, the ChatGPT plan envelope
is limited to 4 MiB; a file below the file-size limit can still require resizing
to fit that route. This is an explicit request rejection, with reference pixels
preserved rather than silently omitted.

The lead's first team turn receives the original user image, intent and estimates.
Shared team context retains them for a specialist's later assigned work; this
does not assign every agent to reproduce the whole design. Multimodal steering
is consumed before the next provider request and uses the same revision/write
guards as text steering. Original user references retain their pixels when old
display snapshots are pruned. Oversized original-reference history rejects the
request; it does not pretend the model saw omitted reference pixels. Attachments
stay in the existing opt-in conversation checkpoints, not in `.ark` geometry,
installers, public fixtures or an additional automatic file store.

A single photograph does not determine unique or exact depth, hidden surfaces,
physical dimensions or manufacturing readiness. The agent must distinguish
observed reference appearance, declared dimensions, assumptions and measured
CAD/mesh validation. B-rep and explicit authored geometry remain the authorities.
Mesh output remains mesh output and carries its own validity limits.

Offline contracts use generated synthetic PNG and JPEG pixels. They cover decoded
dimensions and thumbnails, both intent prompts, blank/partial/full rough sizes,
invalid sizes and corrupted image data, actual request bytes for each supported
transport shape, cancellation before transport, unsupported capabilities,
retained user reference history, and lead/specialist/steering team propagation.
These tests establish local payload and control behavior; they do not establish
reconstruction quality, external model performance, deployment or production
qualification. Studio screenshots and interaction checks are recorded in the
019B iteration verification receipt.
