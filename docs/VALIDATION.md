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
