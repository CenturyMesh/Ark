# Holes Orchestrator — 023B

Milestones **B.02** (editable feature sources) and **B.05** (semantic design and
exchange information) remain **Parcial** beyond the measured local scope.
**C.01** does not become manufacturing-qualified through a nominal catalog.

Functional holes use `native_solid` with `native.op:"holes_orchestrator"` and
one linked exact source input. Sketch circles remain available as nonfunctional
concept geometry. The orchestrator retains one named, editable feature with
named locations and expressions; it cuts cylinders and cones from the exact
B-rep. A tapped hole keeps a thread callout and tap-preparation diameter without
building expensive helical flanks. The report explicitly distinguishes that
representation from a fully modeled thread and from a qualified manufacturing
process. AISI identifies materials, not a thread series.

`holes_catalog` is a read-only, local, paged lookup. Query a family and size to
receive nominal dimensions, evidence URLs, `suggested_native`, `suggested_p`,
the parameters still required and placement instructions. Lookup does not
invoke OCCT, access the internet, change the document or create a drawing.
Use `doc_apply` or `doc_preview` with the observed `expected_revision` for
authorship. Source recomputation and rejected edits use the existing atomic
document protocol; no silent rebase or discarded solids.

```json
{
  "commands": [{
    "op": "feature.add", "type": "native_solid", "name": "Fixing holes",
    "inputs": [1],
    "native": {
      "op": "holes_orchestrator",
      "recipe": {
        "family": "metric", "size": "M6", "kind": "clearance",
        "termination": "through", "fit": "normal", "recess": "none",
        "points": [
          {"name":"left", "position":["HoleX",15,"StockZ"], "direction":[0,0,-1]},
          {"name":"right", "position":[30,15,"StockZ"], "direction":[0,0,-1]}
        ]
      }
    }
  }]
}
```

The example needs one exact stock body whose upper boundary is `StockZ` and
the named parameters. Supply the actual revision separately. Coordinates are
source-local millimetres, independent of assembly placement. Each hole location
must lie on the exact source boundary and its direction must enter stock.
Named positions/directions accept parameter expressions. The native operation
normalizes direction and supports 1–64 unique named locations. Location names
must also avoid inherited `holes.<name>` references when chaining features; the
human creation form chooses the first unused `hole_N` name and keeps existing
names on edits. Through holes
derive their span from the source bounds. Blind holes require positive `p.depth`
and reject a cutter that breaks out of exact stock. Their bottom is flat; the
source does not invent a drill point, tapping runout or engagement allowance.
Blind bores and recesses conservatively require the complete cylindrical envelope
inside stock beyond the entry plane. A curved or stepped mouth can reject even
when a specialized machining strategy could produce a hole there; that geometry
is outside the measured blind/recess scope. Counterbore and countersink shoulders
must remain inside stock even when the central hole is through.

Kinds are `clearance`, `tapped`, `drilled`, `dowel`, `groove_external` and
`groove_internal`. Metric clearance `fit` is `close`, `normal` or `loose`, mapped
to the published ISO 273 fine/medium/coarse nominal diameters. `p.diameter`
overrides the cutting diameter explicitly. For tapped holes, this is the
preparation bore, while `size` remains the thread nominal. `p.pitch` and
`p.thread_depth` are optional; the latter is semantic and cannot exceed the
blind preparation depth. `thread_class` and `fit_callout` are caller declarations
with no invented numeric tolerance limits.

`recess:"counterbore"` requires caller-supplied `p.recess_diameter` and
`p.recess_depth`. `recess:"countersink"` requires `p.recess_diameter` and
`p.countersink_angle` (the included angle in degrees); its axial depth is derived
analytically from those dimensions and the cutting diameter. The orchestrator
does not guess a screw-head standard, seating allowance or material-dependent
process. Inactive dimensions reject instead of remaining hidden in the source.

The metric address space includes every integral nominal M1–M50 and published
small fractional nominals. Published coarse defaults are retained where available;
M50 uses the manufacturer-published **M50x2 fine** default. Missing pitch or
clearance rows require explicit dimensions. Special diameters are never silently
interpolated into an ISO series. UNC includes the published number/fractional
series through 2 inches; UNF extends through 1.5 inches. Fine continuation to
2 inches is **12-UN**, exposed as family `un`, rather than falsely called UNF.
One inch is exactly 25.4 mm. Neither an AISI material designation nor a similarly
sized metric thread is substituted for the requested inch series.
An inch pitch override must equal its published series pitch; custom pitches
reject instead of being silently labeled UNC, UNF or 12-UN.

DIN 471 shaft and DIN 472 bore grooves use manufacturer-published nominal groove
diameter/width through 50 mm. A groove location is a point on the cylinder axis
at the axial start, with width advancing along its direction. The source must
contain the exact coaxial nominal cylinder; the Boolean must remove the complete
declared circular band. External grooves cut an annulus; internal grooves enlarge
the existing bore locally. `p.groove_diameter` and `p.groove_width` are explicit
overrides. Loads, edge margins, material strengths, installed-ring behavior,
fatigue, groove-edge radii and numeric fit tolerances remain unqualified.

The native result must remain one valid positive-volume solid after every cut.
No Boolean fuzzy tolerance is changed, and no body is selected or dropped after
a failed operation. Supported cylindrical hole walls expose `holes.<name>.wall`
and `holes.<name>.axis` exact semantic references. Split cylindrical walls report
split bindings. Native geometry is authoritative; display meshes are derived.
This operation does not preserve a sheet-source flat-pattern lineage. An exact
solid cut is not evidence of a corresponding valid sheet flat pattern.

Primary evidence consulted on 2026-10-14:

- [ISO 273:1979 official scope and current confirmation](https://www.iso.org/standard/4183.html)
  and [publisher-distributed ISO preview, dimension table](https://cdn.standards.iteh.ai/samples/4183/1a8db2e6de054d2e9bed7d40be64d6e1/ISO-273-1979.pdf).
  The catalog retains nominal diameters, not numerical H12/H13/H14 limits.
- [ISO 261 official thread-plan scope](https://www.iso.org/standard/4165.html?browse=ics)
  and [ISO 262:2023 selected-size scope](https://www.iso.org/standard/85105.html).
  Their abstracts identify the standard family, not undisclosed tolerance values.
- [LUBERO metric manufacturer table](https://www.lubero.de/de/technik/gewindenormen/metrischesgewinde),
  [UNC manufacturer table](https://www.lubero.de/de/technik/gewindenormen/unc-amerik-grobgewinde)
  and [UNF manufacturer table](https://www.lubero.de/de/technik/gewindenormen/unf-amerik-feingewinde).
  These supply retained nominal pitch/preparation facts. The erroneous UNF5/16
  preparation value exceeding its nominal diameter is excluded; the published
  [Gühring technical catalog](https://e-catalogue.guehring.com/frontend/catalogs/1197343/1/pdf/complete.pdf)
  supplies 6.90 mm for that row. Manufacturer preparation is not a universal
  cut/form-tap process qualification. For a caller-selected pitch without a
  retained preparation row, nominal-minus-pitch is an explicitly labeled estimate.
- [ASME B1.1:2024 official contents](https://www.asme.org/wwwasmeorg/media/codes-standards/find-codes/c-t_b1_1_2024.pdf)
  distinguishes the Unified coarse/fine series and internal/external classes;
  the complete normative tables were not accessed.
  [LUBERO 12-UN manufacturer products](https://www.gewindeschneider-shop.com/gewinde-schneiden/innengewinde/handgewindebohrer/handgewindebohrer-12-un-amerikanisches-un-gewinde-mit-12-gaenge-steigung/)
  support the fine continuation naming above 1.5 inches.
- [Beneri DIN 471 nominal shaft groove table](https://www.beneri.com/en/prodotto/din-471)
  and [Beneri DIN 472 nominal bore groove table](https://www.beneri.com/en/prodotto/din-472)
  are direct manufacturer evidence. The catalog makes no edition-conformity
  claim. [DIN Media's May 2026 update](https://www.dinmedia.de/resource/blob/1054032/1b5e4d7779f0fa41ea364bf937be5c77/updateverzeichnis-np-konstruktion-data.pdf)
  lists replacement DIN 471/472 editions dated 2026-05. The new full normative
  dimensions/tolerances were not accessed; old edition assumptions are not certification.
- [Basani Venkateswarlu, NJIT Master's thesis (1991)](https://digitalcommons.njit.edu/theses/2643/):
  the accessible university abstract describes parameter-driven tooling hole
  patterns. It supports the design of reusable semantic recipes as an architectural
  precedent. It is not a dimensional standard or an ARK performance measurement.
- [Parametric CAD modeling at Scania, university thesis abstract](https://www.diva-portal.org/smash/get/diva2%3A1107403/FULLTEXT01.pdf)
  connects repeatable modeling practices to design automation. Its accessible
  indexed abstract was consulted; direct full-PDF retrieval timed out. No thesis
  cost/time claim is transferred to ARK.

Verification is defined in `tests/holes_orchestrator_tests.cpp`: local catalog
range/source checks, actual `holes_catalog` discovery, exact multiple through
holes, blind tap preparation, counterbore/countersink volumes, dowel/M50 geometry,
parameter-driven source/locations, DIN circular grooves, saved source recompute,
and explicit atomic/stale rejection. A passing local fixture establishes that
bounded authoring scope; it does not establish deployment, production or physical
manufacturing qualification. No drawing or user-owned project is produced by
this fixture. Build/run results belong in the iteration verification receipt.
These persisted source semantics require document version 38; the release
identity alone does not change a document version.

Observed local verification on 2026-10-14, Windows Release with the native OCCT
worker: the final focused `holes-orchestrator-023b` rerun **passed in 2.09 s** and
the final `studio-holes-orchestrator-ui` rerun **passed in 7.13 s** at 1440x900.
The actual Studio
fixture clicked the tool gallery, consumed native family/position/dimension
controls, created M6 tap preparation with declared 6H, followed a named blind
depth, replaced the same feature with a DIN 471 groove and H7 dowel, retained
density, added another linked hole with a distinct name, and rejected blind
breakout/stale modal commits. Its receipt is
`out/native-release/ui-checks/holes-orchestrator-ui.png.holes-orchestrator-ui.log`.
The native fixture also verified document-38 persistence/downgrade rejection and
excessive through-counterbore depth rejection.

The initial real captures were visually inspected with `view_image`: the family
controls and Apply/Cancel buttons were readable and unclipped, the tree retained
the editable features, and the M6 preparation bore, circular shaft groove and
linked two-hole result were visible. A capture-target defect was found: files
named `*-dimensions.png` showed the first family modal. The selftest now targets
the actual `Hole dimensions` title and retains separate `*-family.png` images.
The corrected 1440x900
dimension images were inspected: declared 6H, named `BlindDepth`, semantic thread
depth, blank groove defaults and declared H7 were readable and their controls
were unclipped. That inspection also found the final word of the long tolerance
hint clipped; the hint was shortened. The final Studio fixtures then passed
**3/3 at 1440x900** and **3/3 at 1366x800**. Compact hole captures were visually
inspected: the full tolerance hint, all six blind-hole controls, declared 6H/H7,
groove defaults, Apply/Cancel buttons and the linked two-hole viewport/tree were
readable and unclipped. Artifacts are under `out/native-release/ui-checks/`
and `out/ui-023b-1366/`, with the compact receipt
`holes-orchestrator.png.holes-orchestrator-ui.log`. The complete integrated
suite passed **115/115 in 198.83 s** before the final wording-only correction;
the affected Studio fixtures were rechecked afterward.

This establishes **Verificado no escopo** for the measured native authoring
fixture while B.02/B.05 remain **Parcial** overall. No provider/model cost
comparison, external-product integration, deployment, physical tap/thread fit,
groove load capacity or manufacturing qualification was measured.
