# 023B research and engineering decisions

Checked on 2026-10-14. This evidence informs B.02/B.05, C.01 and D.02; it
does not qualify manufacturing or establish arbitrary CAD reconstruction.

## Feature recognition and Arkitfy

- Zunaira Sohail, *Analyze fillets, chamfers and holes on 3D geometrical STEP
  file*, MSc, National College of Ireland, 2025:
  [repository](https://norma.ncirl.ie/9782/),
  [full thesis](https://norma.ncirl.ie/9782/1/zunairasohail.pdf).
  The study examines exact surface classification and topological adjacency as
  recognition evidence. Its discussion of ambiguous boundaries supports keeping
  geometric observations separate from inferred feature intent.
- Chandu Tennety, *Machining Feature Recognition Using 2D Data of Extruded
  Operations in Solid Models*, MSc, Ohio University, 2007:
  [university record and abstract](https://etd.ohiolink.edu/acprod/odb_etd/etd/r/1501/10?clear=10&p10_accession_num=ohiou1181406949).
  The record describes hints from extrusion profiles followed by geometric
  validation. The linked PDF could not be retrieved in this research pass;
  conclusions here are limited to the university's abstract.

ARK's resulting design decision is to retain immutable exact exchange geometry,
expose measured recognition candidates to agents, reconstruct explicit parametric
features, and compare the whole result. A match of bounds or volume cannot prove
shape equality. The closed-solid comparison therefore measures both Boolean
differences and retains the complete source census. Unsupported mixed or surface
equivalence stays unresolved. No thesis code, datasets or images are incorporated.

## Functional holes and standards

- [ISO 273:1979](https://www.iso.org/standard/4183.html) identifies clearance-hole
  series. A metric thread size does not itself select a clearance or fit.
- [ASME B1.1-2024 official contents](https://www.asme.org/wwwasmeorg/media/codes-standards/find-codes/c-t_b1_1_2024.pdf)
  identifies the Unified inch thread standard. The catalog keeps the actual
  UNC, UNF or UN designation instead of treating an inch conversion as an
  interchangeable metric thread.
- [AISI's own description](https://shop.steel.org/) identifies its steel-related
  scope. ARK keeps material designation separate from hole/thread geometry.
- Published manufacturer dimensions and current DIN publisher references are
  recorded per family in [Holes Orchestrator](HOLES_ORCHESTRATOR_023B.md).
  In particular, a manufacturer's nominal DIN 471/472 groove table is not
  represented as complete certification against the publisher's 2026 editions.

The implementation uses a local catalog with bounded responses, named positions,
expression dimensions and a native feature dependency. Missing published
dimensions require explicit input. Numeric tolerance limits, tap process,
material compatibility and load capacity remain undeclared unless the caller
supplies the corresponding engineering evidence. Tapped holes use efficient
preparation geometry and semantic thread callouts rather than automatic helical
flanks. Sketch contours remain appropriate for nonfunctional geometry.
