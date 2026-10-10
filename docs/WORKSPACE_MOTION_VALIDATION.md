# Workspace, fabrication intent and mechanical motion verification

Verified locally on Windows with OCCT 8.0.1 on 2026-10-10. The tested native build
is `out/manufacturing-release/Release`. Changes are local on `main`; no commit,
push or hosted CI run is claimed by this report.

| Check | Result |
| --- | --- |
| Native CTest, including sheet and bicycle geometry | 39/39 passed |
| Core-only CTest, including mechanical coupling laws | 26/26 passed |
| Studio context, motion, native, sheet-showcase, sketch, assistant, render, engineering and suspension checks | All nine passed |
| Live Studio/MCP prepared edit, holes, mesh, save, undo, New and disconnect | Passed |
| Hem/emboss MCP create, inspect, formed STEP, save/load and rejected stamped flat STEP/IGES/DXF | Passed |
| Responsive sidebar, grouped ribbon, Layers visibility and Properties/Assistant at 1280x800 and 1600x1000 | Passed |
| Selected blue open-A wordmark after WIC scaling | Pixel color regression passed |

The bicycle checks verify actual material at every one of 48/16 tooth positions,
gaps between teeth, the independent integral of its exact chain-envelope sweep,
and rear-wheel matrices rather than just displayed numbers. At 60 RPM the 48/16
chain and shaft drive the rear wheel at 180 RPM. Changing the rear count to 24
rebuilds its geometry and yields 120 RPM. UI tests additionally exercise cadence
75 RPM, undo, read-only playback and nested assembly collapse/expand.

The sheet implementation supports open round 180-degree hems and circular
conical embosses with constant normal wall thickness and mitred transitions.
Stamped flat geometry is a nominal reference; production flat exports reject.
Mechanical relationships are ideal kinematic chain, gear, shaft, rack/pinion and
harmonic-cam laws. Tooth/cam contact, discrete chain articulation, collisions,
forming strain and dynamics are not qualified.

Real application screenshots: [sheet workspace](studio-workspace.png) and
[bicycle transmission](studio-bicycle-transmission.png). The separate
[design concept](design/sheet-workspace-concept.png) is an image-generation study,
not a screenshot. The selected [wordmark](../assets/ui/ark-wordmark.png) and its
[prompt record](design/ark-wordmark.prompt.txt) came from the built-in image tool.

```powershell
.\start-studio.ps1 -BuildDirectory out/manufacturing-release -Document examples/bicycle-transmission.ark
ctest --test-dir out/manufacturing-release -C Release --output-on-failure
ctest --test-dir out/core -C Release --output-on-failure
```

Use **Model > Motion** to select `Front crank`, set `cadence_rpm`, and play. The
**Teeth** control edits the shared tooth-count and cadence parameters. The
[bicycle walkthrough](BICYCLE_TRANSMISSION.md), [mechanical contract](MECHANICAL_MOTION.md)
and [fabrication intent guide](MANUFACTURING_INTENT.md) specify the supported API.
