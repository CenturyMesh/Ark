# Native Skeleton code — 019B

This scope addresses B.02, D.02 and E.01; each remains **Parcial**. The editor
controls native parametric inputs, relations, vector arrays and repeated features.
It uses a bounded declarative language implemented in C++, with the existing
expression evaluator. It does not execute C++, assembly, scripts or external
programs supplied by a drawing.

Open **All tools → Direct authoring → Skeleton code editor**. **Validate** checks
syntax and scalar relations without changing the drawing. **Preview** evaluates
dependent geometry in an isolated candidate and displays its rendered capture.
**Apply** publishes that checked candidate at its captured revision. Closing the
editor cancels; changes elsewhere invalidate the candidate and reject application.

```text
input span = 120
input rib_count = 4
relation pitch = span / max(rib_count - 1, 1)
relation thickness = span / 30
array stations[rib_count] = [index * pitch, 0, thickness]
pattern Ribs from Seed count rib_count step [pitch, 0, 0]
```

Create a feature named `Seed` before enabling that pattern. Quoted feature names
are supported, for example `pattern "Side ribs" from "Original rib" ...`.
Lengths use millimetres. Existing feature dimensions, datum coordinates and sketch
expressions can reference scalar names or coordinates such as `stations[2].x`.
`index` is the zero-based element index inside an array's vector expressions.

Inputs remain editable with the normal named-parameter tools. Their overrides
persist with the source. Relations, generated array coordinates and pattern
geometry are owned by the Skeleton source and require edits through that source.
Changing an unrelated source line retains input overrides; explicitly changing an
input's default replaces its old override. Pattern IDs remain stable when count
or spacing changes. The editor displays current overrides and ownership.

Scalar relations may depend on arrays once those arrays have expanded. An array's
count must resolve before its expansion: references to coordinates of a later
array reject rather than using an unresolved value as zero. Arrays are expanded
in source order. Counts are integers in 1..256; source is at most 64 KiB, with at
most 256 declarations and the document's existing capacity of 256 parameters,
including generated vector coordinates and unowned parameters. Exact native
patterns have the existing kernel limit of 64 copies; one copy retains the source
B-rep through an identity placement. Mesh patterns retain their own sampled
authority. Unknown commands,
cycles, missing pattern sources, ownership collisions and invalid geometry reject.
Removing the source removes its generated outputs, preserving unowned authoring;
remaining dependencies must still be valid.

`skeleton_inspect` and `skeleton_compile` are read-only agent tools. Geometry
changes use `doc_preview` / `doc_apply` with `skeleton.set{source}` or
`skeleton.remove` and a current `expected_revision`. The team's responsible lead
owns global source edits; specialists can inspect and compile, then coordinate
application through the lead. Focused remains the default team mode.

Skeleton source, overrides, manifests and indexed coordinate expressions require
document format 35. The loader validates ownership against the source before
replacing generated data. Older documents keep their existing version when they
do not use new semantics. Runtime identity is independently defined in
`release.json`.

The iteration receipt records actual local compilation, geometry/persistence
regressions and editor screenshots. This is a native parametric authoring layer,
not a general programming SDK, global geometric solver or production qualification.
