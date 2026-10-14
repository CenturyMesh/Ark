# Edited guided demonstration

The launch video is a **50-second edited guide made from native ARK captures**,
not a recording of an AI session or a benchmark of tool/provider latency.
It opens with the owner's selected drone screenshot, then explicitly moves to
a separate synthetic sensor mounting plate. No owner `.ark` file is included.

Output: `out/launch-demo/ark-guided-demo-016F.mp4`, H.264, 1920×1080, 30 fps,
silent, with MP4 fast-start metadata. The runtime remains 016F / 0.9.17.
This communication evidence supports **F.03** and **F.05**; it does not complete
clean-machine onboarding, a user cohort, or manufacturing qualification.

| Time | Content | Evidence |
| --- | --- | --- |
| 0–8 s | Existing drone workspace | Original `docs/showcase-drone.png`, showing ARK 014D, preserved in full |
| 8–16 s | Synthetic plate before editing | Native Studio capture; 100×65×6 mm; four Ø8 mm holes |
| 16–24 s | Scripted width edit | `param.set plate_width=140`, with current `expected_revision`; same final definition |
| 24–34 s | Before/after inspection | Native B-rep bounds: width 100→140 mm; one valid OCCT solid in each state |
| 34–46 s | Actual Studio BOM | Same edited part, `DEMO-PLATE-001`, quantity 1, nominal aluminium; manufacturing route still needs review |
| 46–50 s | Preview/source and feedback | Repository address and explicit concept scope |

## Reproduce

Use Windows and a native runtime at `out/native-release/Release` (the creator's
default), or pass `--runtime-dir` pointing at an extracted native package or
another build. The observed run used `out/live-workspace-release/Release`.
No network LLM is used. Install video
dependencies into a private output directory, without changing global Python:

```powershell
python -m pip install --target out/launch-demo/tools Pillow==10.4.0 imageio-ffmpeg==0.5.1
python scripts/create-launch-demo.py
```

The creator uses MCP `agent_onboard`, revision-checked `doc_apply`, native
inspection, BOM inspection, concept BOM review and local saves. It creates
synthetic before/after `.ark` files under ignored `out/`, plus JSON receipts.
Density/appearance are illustrative declarations; no supplier, production
method, tolerance, mating interface or complete fastener set is asserted.

Capture both synthetic states, waiting for each process to finish:

```powershell
$studio = (Resolve-Path out/native-release/Release/ark-studio.exe).Path
Start-Process -FilePath $studio -WindowStyle Hidden -Wait -ArgumentList '--load out/launch-demo/plate-before.ark --shot out/launch-demo/plate-before.png --settings-file out/launch-demo/headless-settings.json --size 1600 1000'
Start-Process -FilePath $studio -WindowStyle Hidden -Wait -ArgumentList '--load out/launch-demo/plate-after.ark --shot out/launch-demo/plate-after.png --settings-file out/launch-demo/headless-settings.json --size 1600 1000'
```

For the BOM image, open **a separate owned Studio instance**, with its own
`--settings-file`, load the synthetic before file, then connect MCP to its
`--live-endpoint-file`. Read `doc_info`, apply `plate_width=140` with that exact
revision, read `bom_inspect`, and open **Manage → BOM**. Capture that actual
dialog as `out/launch-demo/plate-bom.png` using Computer Use, then visually
verify its part number, quantity and pending manufacturing route. Do not use a
self-test BOM, the owner's open drawing, or a mockup. This capture step is
deliberately a manual/Computer Use step rather than simulated application UI.

Set `ARK_UPDATE_LAUNCHED=1` **only in the child launch environment** if an
installed update would redirect a development Studio invocation; restore the
previous environment immediately after launching. Headless `--shot` does not
redirect. The initial capture run discovered a redirection to an owned 0.9.17
instance; that instance was closed and the explicit native build relaunched.
The owner's existing Studio remained open and untouched.

```powershell
python scripts/render-launch-demo.py
```

The renderer reads the selected drone image without writing it, scales whole
captures into frames, adds separate explanatory typography, and encodes six
chapters. It also creates the chapter PNGs and `storyboard.png` for review.
Publish the reviewed MP4 as a release asset; the synthetic `.ark` files and
raw local receipts remain local.

## Observed verification and limits

- Native inspections returned `brep_valid:true`, `solids:1` and bounds
  100×65×6 mm / 140×65×6 mm. The width-linked right holes moved with the edit.
- A live Studio edit used revision 1 and returned revision 2; the BOM at r2
  retained feature 11 / `DEMO-PLATE-001` and quantity 1.
- BOM concept review explicitly retained fabrication and hardware questions.
  The displayed table shows **Needs review** and the missing manufacturing route.
- All source captures and chapter layouts were visually inspected. The final
  MP4 decoded without errors, with a 50-second duration and 1500 video frames.
- Preparation had two rejected MCP attempts: a fillet missing the explicit
  selection, and a `native_inspect` string ID. Both rejected without changing
  document state; corrected inputs used `selection:all` and numeric ID 11.
  A parameter-dialog automation attempt failed to set the field; it was
  cancelled without applying a change. These attempts are not shown as success
  in the video and are not timing/cost evidence.
- The full native screenshots automatically fit each state independently.
  Captions therefore report measured dimensions rather than deriving scale
  from pixels. This demo makes no fabrication, drone-performance or AI-cost claim.
