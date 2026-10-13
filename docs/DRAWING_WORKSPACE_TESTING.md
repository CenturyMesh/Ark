# B.05 drawing workspace local verification

The human workflow is **Manage → Drawings**: select an exact source solid,
create a sheet, author orthographic/section views and semantic dimensions with
declared tolerances, inspect the native paper preview and export SVG/JSON.
It uses the existing `drawing.set`/`refresh`/`remove` revision contract.

The native UI test clicks the rendered Manage card, invokes real window buttons,
fills the native edit/combo controls and inspects the authored document and
derived geometry. Its fixture is a synthetic exact box, initially 50×30×12 mm.
Drawing operations preserve its B-rep. An explicit fixture model edit changes
the width to 65 mm, which makes the sheet stale until the Refresh source button
updates it. A simulated intervening edit between form acceptance and commit
demonstrates that a stale form cannot silently rebase.

Configure the native build with `ARK_TEST_STUDIO_LIVE=ON`, build `ark-studio`,
then run:

```powershell
ctest --test-dir out/native-release -C Release `
  -R '^studio-drawing-workspace-(1180|1440)$' --output-on-failure
```

Equivalent individual runs use `ark-studio --drawing-workspace-test --shot
<new-local-prefix>.png --size 1180 900` or `--size 1440 900`. The test writes a
receipt ending in `.drawing-workspace-test.log`, `.drawing-workspace.png`,
`.drawing-final.png`, `.drawing-stale.png`, `.dimension-form.png` and synthetic
`.drawing.svg`/`.drawing.json` exports beside the chosen prefix. No user document
or saved settings are included in these artifacts.

The tested flow covers native source/sheet selection, orthographic and exact
section forms, semantic dimensions and tolerance editing, source stale state,
explicit refresh, cancellation, revision conflict, view rename association and
local SVG/JSON publication. The captures are actual native window renders:

- [1180 px sheet](images/drawings/drawing-workspace-1180.png)
- [1440 px sheet](images/drawings/drawing-workspace-1440.png)
- [Dimension/tolerance form](images/drawings/drawing-dimension-form.png)
- [Stale source state](images/drawings/drawing-stale.png)
- [Display zoom](images/drawings/drawing-zoom-1180.png)

Zoom +/− and Fit are exercised as native buttons. The test verifies that their
visual 0.5×..4× scale changes do not edit the document revision, authored sheet
scale or measured dimension values. Zoomed paper is clipped to its viewport.
The test then sends a real mouse down/move/up drag to pan the zoomed canvas,
checks that all three realized view and label bounds are accessible, and verifies
that Fit resets pan and zoom while preserving authoring and measurements.

These tests verify the bounded local human workflow. B.05 remains **Parcial**:
assembly drawing sources, arbitrary auxiliary views, hatching, GD&T, interactive
paper dragging/snapping, PDF/DXF drawing exchange and formal drafting or
manufacturing qualification remain outside this slice. The workspace minimum
is 1100×700; the complete workflow and forms are verified at the two 900 px
heights above. Other viewport sizes are not claimed as verified.
