# ARK handbook

This guide takes you from a first editable model to assemblies, agent assistance
and exchange. The [documentation index](README.md) links detailed contracts and
recorded evidence. The application identifies itself from [release.json](../release.json);
older documents may describe a specific historical iteration.

## 1. Build and open Studio

Studio is a Windows x64 application. Install Visual Studio 2022 Build Tools with
C++ and the Windows SDK, CMake 3.21+, and use an OpenGL 3.3-capable desktop.
From a source checkout:

```powershell
.\scripts\build.ps1 -Test
.\start-studio.ps1
```

The first native build obtains and builds the pinned OCCT 8.0.1 SDK. Its download,
source and build cache live under ignored `out/deps/occt`. ARK itself builds into
`out/native-release`; the Release executables live in its `Release` directory.
For a tested rebuild before opening the window:

```powershell
.\start-studio.ps1 -Rebuild
```

You can supply `-BuildDirectory` for another compatible native build tree.
[Build reference](NATIVE_BUILD.md).

The portable core is built with `-DARK_WITH_OCCT=OFF`. It is useful for headless
core work, but does not provide native STEP modeling, Studio or Linux equivalents
of Windows account/network transports. See [G.04 in the roadmap](ROADMAP.md#g--advanced-platform).

## 2. Find your way around

The ribbon groups operations by workspace. **All tools** opens a searchable gallery;
**Commands** (`Ctrl+K`) finds the same commands by name. Use the model explorer to
find a feature or assembly occurrence and its inspector to edit it.

| Action | Control |
|---|---|
| New / open / save | `Ctrl+N` / `Ctrl+O` / `Ctrl+S` |
| Undo / redo | `Ctrl+Z` / `Ctrl+Y` |
| Find a command | `Ctrl+K` |
| Frame the model | `F` |
| Orbit / pan / zoom | Drag / Shift+drag / mouse wheel |
| Standard views | View cube; keys `1`–`7` for front, back, top, bottom, right, left and isometric |
| Contextual actions | Right-click a viewport body or model-explorer row |

Selection can target Body, Face, Edge or Curve. Pick the intended level before an
operation that needs a semantic reference. **Spotlight body** temporarily isolates
an occurrence; **Restore previous visibility** returns to the earlier display.
Spotlight changes visibility, not geometry or document revision.

To inspect an unfamiliar design, fit it, expand its explorer, select a feature and
read its properties before editing. [Interaction reference](STUDIO_INTERACTION.md).

## 3. Make a first editable part

A useful first exercise is a mounting plate with a hole:

1. Start a new document and choose a sketch on XY.
2. Draw a closed rectangle. Add dimensions or parameter expressions to control its
   width and height rather than relying on an approximate screen shape.
3. Create an exact extrusion from the closed profile and enter its thickness.
4. Add a hole through the plate with the supported native hole operation, or use a
   circle on a suitable plane and a supported exact cut.
5. Inspect the result, save it as a new `.ark` file, then change a source dimension.
6. Confirm that dependent geometry recomputes. Undo and redo the change.

If a profile, reference or result fails validation, use the diagnostic to correct
its input. A larger tolerance or an unrelated mesh replacement does not repair the
intended exact model. The sketch and feature history should explain how the plate
was constructed.

For an existing parametric model, open:

```powershell
.\start-studio.ps1 -Document .\examples\drone-hull.ark
```

Inspect its source parameters and feature dependencies, save a separate copy and
make a small dimension change. The hull, openings and repeated motor-arm definitions
show how design relationships survive editing. [Sketch reference](PARAMETRIC_SKETCH.md)
and [native operation reference](NATIVE_STUDIO.md).

## 4. Keep exact geometry and display geometry clear

ARK has exact native bodies and explicit mesh bodies. OCCT evaluates supported
native solids and surfaces as B-reps; Studio displays tessellated previews of them.
The visual mesh is not the exact STEP authority.

Mesh import takes an STL/OBJ snapshot. Mesh tools such as weld, smooth, decimate,
offset and fill operate under their own sampled-geometry contracts. Explicitly
sampling a native body produces a mesh result; it does not preserve exact surface
or manufacturing authority. A mesh offset is a vertex-normal operation, not a
certified constant-thickness shell.

For native geometry, use the native inspector and exact exchange tools. For mesh
geometry, use mesh inspection and preserve its approximation limits.
[Surfaces and mesh contracts](SURFACING.md).

## 5. Build an assembly

Create or reuse part definitions, insert instances, and arrange them in groups.
An instance places a definition; several instances can share the same underlying
part. Editing that definition changes its copies without authoring separate solids.

Ground a suitable anchor, then add mates between supported named references.
Inspect mate residuals and remaining degrees of freedom. A visually plausible pose
is not a solved assembly if references are missing, the system is unanchored or
constraints conflict.

Supported joints and couplings can drive read-only motion previews. Direct dragging
moves supported joints; use **Move component** when the intended motion is ambiguous.
Persist a pose through the supported revision-checked assembly command. Preview
animation does not establish collision-free motion, dynamics or product safety.

Try [six-axis-robot.ark](../examples/six-axis-robot.ark) for a parameter-driven
hierarchy, or the [KITE suspension](../examples/kite-17-suspension.ark) for a checked
kinematic domain. [Assembly contract](MECHANICAL.md),
[manipulation](ASSEMBLY_MANIPULATION.md) and [motion](MECHANICAL_MOTION.md).

## 6. Work with sheet metal and frames

Authored sheet programs retain a base panel, ordered flanges and panel-owned
openings. **Formed**, **Flat** and playback change display poses. They do not
change document intent or certify a bend trajectory.

Open [sheet-flange-tree.ark](../examples/sheet-flange-tree.ark), inspect its panel
and flange inputs, and compare formed and flat views. Choose the intended pose
explicitly when exporting. Sheet flat length depends on the caller's K factor;
material calibration, springback and general tool-access qualification remain separate.

Source STEP import supports bounded domains. Imported sheets currently require
one uniform-thickness panel or two rigid flanges with one complete straight
cylindrical bend. Unsupported or ambiguous imports reject explicitly. General
multi-bend imported-sheet unfolding is not an available promise.

Construction Frames provides supported round/rectangular members, joints,
trim/extend and cutlists. Read the ownership and eligibility rules before applying
end cuts or weld features. [Sheet reference](SHEET_METAL.md),
[source import](SOURCE_STEP.md) and [frame/sheet operations](FRAME_SHEET_OPERATIONS.md).

## 7. Inspect materials and measurements

Materials distinguish appearance from physical properties. Color, opacity,
metallic/roughness and texture change the display. Density supports mass and
centre-of-mass results; elastic properties support the documented analytical models.
Missing material data must remain visible rather than becoming an invented result.

Open **Inspect → Measure 2D**, **Measure 3D** or **Units**. A 2D measurement uses
its displayed sketch plane; 3D measurements use world coordinates and assembly
placement. Native surface picks use exact B-rep intersections. Mesh-only picks are
labelled as display estimates. Temporary measurements do not create dimensions or
edit geometry; a changed revision or display preview clears stale annotations.

Display-unit preferences do not change internal millimetre contracts. Read each
physics report's coordinate frame, units, assumptions and unresolved checks.
These tools are not a general FEA solver. [Measurement](MEASUREMENT.md),
[materials](RENDER_MATERIALS.md) and [engineering tools](ENGINEERING_AGENT_V2.md).

## 8. Maintain the BOM

Open **Manage → BOM** or **Inspect → BOM**. Search or use All / Needs review / Buy /
Make filters, then inspect a selected part. Set the internal part number and a
useful description on each final physical definition. Add material, make/buy intent
and sourcing references when known.

Quantities come from resolved physical occurrences and pattern copies, including
hidden instances; they are not a separately edited list. Without an assembly,
terminal physical outputs form the inventory. Consumed construction features are
not extra parts. Sorting and filtering retain the original definition IDs.

A source URL or selected supplier is declared metadata. It does not prove stock,
standards compliance, price or an issued purchase order. **Copy link** copies a
stored URL; it does not fetch the source.

Export the versioned JSON BOM when needed. The current BOM export is local; a
MeshWorks adapter is planned. Agent completion review is revision bound: detailed
scope rejects unresolved metadata; concept scope discloses pending fields and its
fastening review. Any subsequent edit can make the review stale. [BOM contract](BOM.md).

The component library searches locally, retains original STEP bytes and provenance,
and inserts through the same validation and revision path. Original geometry and
declared catalog data remain distinct. [Library reference](COMPONENT_LIBRARY_ARCHITECTURE.md).

## 9. Choose an agent connection

There are two ways to work with models:

- **In-app Assistant:** configure its provider, actual model and authentication.
  Single runs one assistant; Team provides an engineer, designer and reviewer.
- **External MCP client:** run `ark-mcp` as a tool server. The client owns its model
  login; ARK owns the document tools.

The [connection guide](CONNECTING.md) explains ChatGPT authorization, API settings
and live MCP. Modeling itself requires no API key. A ChatGPT account route, an API
key route and an external client's account are separate connections; ARK does not
silently switch billing modes.

Focused is the default team mode. The engineer builds and delegates concrete tasks
sequentially. Parallel is optional, with serialized document publication in either
mode. Before asking for an open design, state its purpose, envelope, interfaces and
any required material or fabrication process. If those choices are still open,
ask for an editable concept and explicit assumptions.

The agent must inspect the current document, use supported operations, preserve
revision checks, review failures and report completion with evidence. A conversational
claim or a role becoming idle is not a completed design. Provider-reported token
usage can be partial or missing; missing usage is not zero cost.
[Team controls](MULTI_AGENT_STUDIO.md) and [agent activity](AGENT_ACTIVITY.md).

For live external editing, copy the active endpoint from the intended Studio
process. Without `--connect`, MCP owns an independent document. After a restart,
refresh the endpoint and confirm the current document with `agent_onboard` or
`doc_info`. If a connection drops after an edit was dispatched, inspect the revision
before retrying: the edit may already have completed. [Live bridge](CODEX_CONNECT.md).

## 10. Save and exchange your work

Keep `.ark` as the editable design. It retains authoring intent and persistent
iteration history; exchange files serve other purposes. A runtime upgrade does
not by itself change a document's format version. Save a copy before testing an
older runtime against newer document features.

| Output | Preserves | Important boundary |
|---|---|---|
| `.ark` | Parametric authoring, metadata and supported history | Document compatibility depends on the actual persisted features |
| STEP / IGES | Supported exact selected-feature geometry | Part-definition coordinates; not a whole-scene CAD assembly export |
| STL / OBJ / FBX | Visible or selected mesh geometry with baked placements | No editable feature history or exact surfaces |
| BOM JSON | Current versioned part/occurrence inventory and metadata | No B-reps; no remote industrial adapter implied |
| `.arkops` | A replay sequence of public tool calls | A failed line stops replay; earlier successful batches remain committed |

Exact exchange has its own readback checks. Select the correct feature and pose,
read the reported limitations and retain the source document.
[Exchange](EXCHANGE.md), [formats](FORMATS.md),
[document container](DOCUMENT_CONTAINER.md) and [history](ITERATION_HISTORY.md).

## 11. Updates and troubleshooting

**ARK updates** can prepare verified Windows packages for next-launch activation
and rollback. Save normally before closing the current session. The trusted online
channel still requires deployment and a real end-to-end test; local staging is
not proof that public automatic delivery is available. [Update contract](UPDATES.md).

| Symptom | Next useful action |
|---|---|
| Native build cannot locate OCCT | Check the MSVC SDK path and toolchain in the build reference; avoid mixing MSVC and MinGW libraries |
| Unsupported or ambiguous geometry | Capture the input, failing command and native diagnostic; correct the source/contract |
| Stale revision rejection | Read the current document, reconsider the proposed edit and submit against its observed revision |
| Agent edits do not appear in Studio | Verify live `--connect`, the active endpoint and document identity |
| Model call fails | Inspect connector/model/authentication status; preserve redacted request diagnostics |
| BOM review is incomplete | Read all item pages at one revision and resolve or explicitly disclose required fields |
| An older runtime rejects a document | Use a compatible runtime; preserve the original file rather than removing unsupported data |

For reports, provide the iteration/runtime, platform, minimal reproduction and
actual diagnostic. Share only authorized files and redact credentials. Use the
[contribution guide](../CONTRIBUTING.md) and [security policy](../SECURITY.md).

ARK source is licensed under [Apache-2.0](../LICENSE). Third-party components
retain their own terms and notices. [Dependency notices](THIRD_PARTY_NOTICES.md).
