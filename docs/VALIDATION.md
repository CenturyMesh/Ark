# Validation record

Local validation on 2026-09-22, Windows, GNU C++ 16.1.0 / MinGW-w64, CMake Release build.

| Check | Result |
| --- | --- |
| Clean CMake compile of kernel, CLI, MCP, Studio and tests | Passed |
| Existing kernel regression suite | Passed |
| New agent/math/mesh regression suite | 196 checks, zero failures |
| Real MCP stdio integration | Passed: initialize, discovery, notification handling, malformed JSON, revision conflict, atomic edit, save/load, STL and PNG |
| CTest | 3/3 suites passed; approximately 0.45 s on the local machine |
| Native Studio UI dispatch smoke | Create, undo, redo, delete, wireframe, grid and camera actions passed |
| GPU rendering | Actual executable captured at 1600x1000 and 1180x840; OpenGL error check passed |
| Native replay | Harmonic `.ark` loaded, `.arkops` replay changed waves/amplitude and rendered the revised mesh |
| Agent visual feedback | Software-generated PNG inspected after correcting perspective division |
| Harmonic sample | 10240 vertices, 20480 triangles, one indexed connected component, zero boundary/nonmanifold edges and zero degenerate triangles |

The screenshot in [README](../README.md) comes from the actual native application, not a design mockup. Headless frames show zero FPS because they do not run the interactive timing loop. Recompute milliseconds shown in screenshots are a single local sample, not a benchmark guarantee.

UI checks exercise the application's action dispatch, not a full mouse/keyboard automation suite. Common file dialogs and the modal text editor have not been exhaustively automated. The topology report does not test self-intersection, vertex manifoldness or certified solid validity. These tests are initial regression evidence, not CAD production acceptance.

Reproduce:

```powershell
.\scripts\build.ps1 -Test
.\out\build\ark-studio.exe --self-test-ui --shot out/studio.png --size 1600 1000
.\out\build\ark-studio.exe --load examples/harmonic-ring.ark --script examples/refine.arkops --shot out/refined.png --size 1180 840
```

On Windows the GUI executable may return control to PowerShell before it exits; use `Start-Process -PassThru` and wait on that process when scripting. The GitHub Actions workflow builds and tests Windows/MSVC and Linux/GCC; consult the repository's workflow run for current CI status.

## GitHub Actions availability

The first [workflow run](https://github.com/CenturyMesh/Ark/actions/runs/35763704970) did not start a runner. GitHub reported failed account payments or a spending limit requiring an increase. The Windows job has no executed steps; Linux was canceled by the matrix after that infrastructure failure. Windows/MSVC and Linux/GCC have therefore not been validated by CI. Local MinGW tests and native rendering above passed independently. No billing settings were changed.

## Version 0.3

The record above describes the initial 0.2 baseline. For current assembly/tube/CUDA test evidence and timing, see [MECHANICAL.md](MECHANICAL.md). The launcher and build script now default to `out/next`, keeping the running 0.2 binary intact during this upgrade.

## Version 0.4

The integrated build defaults to `out/fabrication`. New C++ notch tests check analytical cut-face closure and its inverse, independent volume integration, side orientations and invalid envelopes. Optional OCCT integration tests validate four retained-wall sides, STEP readback, B-rep validity, retained material and rejection of modified cutters. See [NOTCH_WORKFLOW.md](NOTCH_WORKFLOW.md) for the explicit geometric/physical boundary.

The neutral UI was built and visually checked at 1600x1000 and 1180x840. Native in-app fixtures exercise property, placement and command dialogs plus acceptance/cancellation, creation and undo. The UI and assembly smoke modes returned zero exit codes. External click automation was unavailable because the native computer-use pipe could not connect; these checks do not claim an exhaustive external interaction test.

## Version 0.5

The integrated build defaults to `out/bend`. Six native/stdio CTest suites pass. Additional native workspace/export fixtures and a real Windows STEP worker test cover the complete editable-tube workflow, export from the folded pose, Unicode output paths and backend failure. See [BEND_WORKSPACE.md](BEND_WORKSPACE.md) for scope and reproduction details.

## Version 0.6

The integrated build defaults to `out/source`. Seven CTest suites pass. Optional OCCT tests cover an independently constructed synthetic source, rigid-transform metamorphism, rejection of a hole on a deforming hinge, source import/save/load/export, source drift and read-only geometry. Native source workspace tests verify formed/straight rendering, opening focus, unchanged source data during animation and export. The real source is verified locally; its CAD and derived assets are excluded from Git. Details are in [SOURCE_STEP.md](SOURCE_STEP.md).
