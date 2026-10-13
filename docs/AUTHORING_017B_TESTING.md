# Test the 017B draft

Run the isolated local preview after it has been prepared:

```powershell
cd C:\Sw_templates\Ark
& .\out\test-preview\r4\ark-studio.exe
```

The earlier `r1`, `r2` and `r3` folders are independent. To prepare another copy from a completed
native build, run `scripts/prepare-test-preview.ps1 -Name r5`. An existing preview
is never overwritten. With the normal MSVC build, pass
`-BuildDirectory out/native-release`.

To open the synthetic example in a fresh Studio process:

```powershell
& .\out\test-preview\r4\ark-studio.exe --script .\examples\authoring-017B.arkops
```

Search the following commands in **All tools**. Test on copies of existing work
and retain any reproducible failing operation or project activity receipt.

| Workflow | Steps and expected result |
| --- | --- |
| Continuous lines | Draw sketch → Line. Click four corners and the initial endpoint. Each click starts the next segment; snaps remain visible. The closed loop shades. Enter ends this chain; another click starts a new one. Escape exits. Undo removes one committed segment. |
| Dimensions | Select a saved line, circle or arc → Parametric curve dimension. Enter a named parameter/expression. Edit that parameter and verify recomputation. Click a dimension label to edit it. Rectangle width/height and existing constraint diagnostics remain available. Spatial curves accept a 1-based segment number and line length/arc radius. Recipe arcs use their point expressions. |
| Equation curve | XYZ equation curve: `X=R*cos(t)`, `Y=R*sin(t)`, `Z=5*t`, `t=0..2*pi`, 64 samples. Create parameter R first. Edit R or the feature and confirm persistence after save/reload. |
| Projection and trim | Project curve onto surface: source curve ID, target surface ID, direction `[0,0,-1]`. Trim surface by closed curve: surface ID, projected closed-wire ID, keep point on its interior. The example uses `[10,10,1]`. Boundary/outside points must reject without changing the revision. |
| Offsets | Offset saved sketch curve: named source, signed expression. Positive CCW circle distance shrinks it. Offset exact solid: positive grows, negative shrinks; intersection/arc joins. Existing Offset selected surface and mesh normal offset commands are also in All tools. Collapsed results must reject. |
| Selection | Select a named face or edge, then Jump to adjacent face / edge. Confirm highlight and reference identity. Agents use `reference_navigate` and `selection_set`; supply occurrence/copy for assemblies. Hidden/consumed geometry is identified in the selection receipt. |
| Planes | Create or enable construction planes, toggle datum frames, and inspect transparency, model-scaled extents and readability at different camera distances. |
| BOM batch | Manage → BOM → Export parts. Choose a new output directory under an existing parent, formats such as `["step","iges","stl"]`, All/Make/Buy/Selected part and formed/flat sheet pose. Inspect manifest.json, bom.json, file hashes and quantities. A failure must publish no partial directory. |
| Purchased components | Ask a configured agent for a specified bought component and its supplier URL. It should call `library_source_search` with source `web`, read the actual URL with `library_source_page`, and persist manufacturer/reference/source_url in part metadata. Search snippets alone are candidates. Challenges or missing specifications remain unresolved. |

STEP and IGES retain exact exchange authority; mesh formats are display meshes.
IGES validation reports surface bounds/area and whether solid topology survived.
Equation interpolation and sampled sketch/mesh offsets have the limits described
in [017B](iterations/017B.md). No production qualification is implied.

The PR stays in draft until the user finishes these tests and explicitly asks
to advance it. It must not be merged automatically.
