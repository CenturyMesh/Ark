# ARK — Agent-Ready Kernel

**Parametric CAD for people and agents.**

Studio 0.8 uses a compact document bar and floating tool dock instead of a
ribbon. **Commands** (Ctrl+K) opens grouped actions for documents, sketches,
solids, native surfaces, sheet metal, tubes, assemblies, views and agents.
Existing operations remain available there; **All tools** opens the same menu.
New is also available through Ctrl+N. [Native surface and sheet controls](NATIVE_STUDIO.md).

## Model a part

1. Choose **Plane** to create a named construction frame with expression-based
   offsets and rotations. XY, XZ and YZ are built in.
2. Create a **Rectangle**, **Circle** (in Commands) or **Spline** on that plane.
   Dimensions and anchor coordinates can refer to named parameters.
3. Choose **Extrude** for a closed outer profile. For openings, create a separate
   sketch of circles or closed curves and choose **Cut sketch**, supplying the
   base feature and depth/offset.
4. Edit named parameters in the left panel or through **Named parameter**.
   Click a saved sketch to inspect it and show its points/curve overlay. The
   plane view faces its construction plane directly.

The spline form accepts short lists such as
`-width/2,0; 0,-height/2; width/2,0; 0,height/2`. Expressions may contain nested
functions: `max(-40,-30),0` is one anchor. Closed is a checkbox; tolerance and
tension remain explicit numeric settings. Adding a shape to an existing sketch
name appends it. Advanced entity/constraint edits use the sketch JSON editor;
interactive dragging and a complete visual dimension-constraint editor are not
implemented in this milestone. [Geometry contract](PARAMETRIC_SKETCH.md).

Open the example:

```powershell
.\start-studio.ps1 -Document .\examples\freeform-plate.ark
```

**Graphics** opens toggles for the ground grid, wireframe, light background,
satin highlights, ambient occlusion, visible edges, and an orthographic camera.
These are display preferences stored in `%LOCALAPPDATA%\ARK\studio.json`. They
are not part of the `.ark` document. Extrusions and sketch cuts use hard
surface normals so flat caps do not acquire misleading smooth shading.
Curve and surface geometry is unchanged by these settings.

The view toolbar sits on the bottom edge of the 3D view: **Orbit**, **Pan**,
**Zoom**, **Fit**, **Front**, **Top**, **Right**, **Iso**, **Edges**, **AO**,
and **Ortho**. Dragging orbits, pans, or zooms according to the active mode.
Shift+drag and the middle button still pan, and the wheel still zooms. Named
faces (Front, Back, Top, Bottom, Left, Right) are orthographic. **Iso** returns
to a perspective view and frames the model. Keys `1` through `7` are front,
back, top, bottom, right, left, and isometric. The view cube in the upper right
snaps to the clicked face; dragging it orbits. Ambient occlusion is a
screen-space pass over the 3D view. If the framebuffer is unavailable, Studio
keeps the clay shading and skips that pass. Visible edges are the feature
polylines already used for picking, drawn with depth so hidden edges stay
hidden.

New/Open retain the unsaved-document guard. Save/Open use native file dialogs.
Successful operations from a connected agent update the same scene and undo
history; successful document loads/saves also update the displayed filename.

## Work with an agent

The assistant panel has a native text input, **Send**, **Cancel**, **Connect**
and **Codex MCP** controls. Ctrl+Enter sends a message. Connect configures a
Responses-compatible endpoint, an explicit model name, and the name of the
environment variable holding its credential. It does not store an API key.
Sending transmits the prompt and requested tool context to that configured
provider. [Connector setup and limits](ASSISTANT.md).

Network work runs on a worker; document operations are serialized on the UI
thread. The assistant can inspect and modify this same document. Human edits or
replacement of the document invalidate stale assistant work. Cancellation stops
pending requests; it does not undo already committed geometry changes.

**Codex MCP** shows the live endpoint for an external agent. This is a separate
connection from the in-app API chat and uses the same open document without
requiring an API key in ARK. Call `agent_onboard` first.
[Codex configuration](CODEX_CONNECT.md).

Preferences are stored separately from CAD files at
`%LOCALAPPDATA%\ARK\studio.json`. Conversation history is in memory. Tests can
use `--settings-file <path>` to isolate their settings.

## Reproduce UI checks

The native smoke modes exercise the real command dispatch and dialogs, with
automatic fixture input. They are not an exhaustive external click test.
Run them on a Windows machine with an OpenGL 3.3 context. The C++ live Studio
process test is explicit/opt-in and is not part of the headless CI gate.

```powershell
.\out\native\Release\ark-studio.exe --self-test-sketch-ui --shot out\sketch-ui.png --size 1180 840
.\out\native\Release\ark-studio.exe --self-test-assistant-ui --shot out\assistant-ui.png --size 1180 840
.\out\native\Release\ark-studio.exe --self-test-native-ui --shot out\native-ui.png --size 1180 800
.\out\native\Release\ark-studio-live-tests.exe .\out\native\Release\ark-studio.exe .\out\native\Release\ark-mcp.exe .
```

The assistant smoke uses a local injected response fixture and makes no paid API
call. The C++ live integration test starts its own temporary Studio process and
independent MCP proxy processes, checks actual parameter/geometry/save/undo/New
behavior, then verifies that disconnect never falls back to another document.
It does not control an existing operator window.
