# Drawing delivery and the component library — 067G

**B.05 / B.04 / F.03 — Verificado no escopo local.** See the [iteration evidence](iterations/067G.md). Broader milestones remain **Parcial**; local checks do not establish manufacturing approval.

## Vector PDF and ordered sheet sets

`drawing_export` accepts `format: "pdf"`, `expected_revision`, a new `path`, and
exactly one `name` or an ordered `names` array of 1–256 distinct sheet names.
Every sheet must have a current exact source binding and resolved dimensions.
One invalid or stale sheet rejects the entire export before publication. Existing
destinations are never replaced, including concurrent publication attempts.

Each sheet keeps its own paper dimensions. The PDF contains vector paths and text,
page numbers and `/PrintScaling /None`; print at **Actual size / 100%** in the
recipient's PDF viewer. Physical printer calibration and non-printable margins
are outside ARK's local verification. The Studio Export dialog selects the format
and either the current sheet or all sheets in document order.

PDF and SVG use the retained display polylines derived from exact drawing curves.
They are presentations, not exact geometry exchange; use the existing DXF route
for its separately documented canonical curve contract. PDF text supports Latin-1
labels and rejects other scripts explicitly instead of replacing characters.

## Authored geometric tolerance frames

A drawing may retain up to 64 `geometric_tolerances` entries. Each has a unique
`name`, existing `view`, `characteristic`, positive numeric `value`, matching
sheet `units`, zero to three uppercase `datums`, and paper-coordinate
`position_mm` / `leader_mm` pairs. Characteristics include position, flatness,
straightness, circularity, cylindricity, parallelism, perpendicularity, angularity,
line/surface profile and runout/total runout. Form tolerances reject datum labels.

The **Geometric tolerance** button in Manage Drawings authors or replaces a frame
by name, or removes it explicitly. Frames appear in Studio, SVG, PDF and the
derived DXF annotations. Unit edits preserve physical tolerance values; view
renames preserve annotation references. New frames require document semantics 56;
ordinary older drawings retain their existing version.

These are caller-authored requirements with named characteristics, not a certified
ISO/ASME symbol layout, datum-system solver or inspection result. Leader positions
are authored presentation coordinates; they do not establish an exact face datum.

## Visual parametric component library

The **Component library** command opens a local folder of immutable `.arkprt`
parts and `.arktop` assemblies. Subfolders supply categories. Search matches the
name, filename, category and public parameter names; a kind filter separates parts
and assemblies. Selection displays the exposed parameter expressions and an
isometric thumbnail derived from evaluated geometry, with assembly placement.

The agent-facing existing `parametric_components` tool gains two read actions:

- `library`: explicit `directory`, optional `query`, `category`, `kind`.
- `thumbnail`: explicit component `path`; returns normalized display line segments
  and the package digest.

Catalog scans skip symlinks, examine at most 4096 entries and four subdirectory
levels, and return at most 256 matches with a `truncated` flag. Malformed packages
are reported separately. Thumbnail sampling is declared and is display-only.
Neither catalog scans nor thumbnails import a part or alter the open project.

Insert and Update prepare the existing native candidate, show the checked result,
and apply only that candidate at the observed project revision. A file changed
since the catalog was read rejects until refresh. Updates require the same
component identity and its next explicit revision, preserving existing ownership,
local-edit and structural-change rejection rules. No network lookup or automatic
update runs. Catalog navigation and geometry preview execute through the Studio
worker/progress route.

## Local verification

- `associative-drawings` passed the existing exact source/export checks plus PDF,
  ordered two-sheet delivery, authored frame persistence, semantic downgrade
  rejection, stale-sheet atomic rejection and no-replace publication.
- `parametric-components-053b` passed 106 assertions, including public-parameter
  catalog search, evaluated thumbnail and stale source-digest rejection.
- `studio-component-library-ui` exercised native search, insertion with public
  overrides and explicit revision update; normal and 960�640 captures were
  inspected. `studio-drawing-workspace-1180`, its 1440 variant and
  `studio-drawing-viewport-ui` passed; frame authoring, PDF export and affected
  controls were inspected in captures.
- Independent PDF readback identified one A4 sheet with 72 vector paths and an
  ordered two-sheet A4 set with 72/90 paths, all with extractable text and zero
  embedded images. All three rendered pages were inspected after correcting
  label/leader placement. Local printer output and independent drafting-standard
  qualification were not tested.

Evidence files are local under the build's `verification-067g` and `ui-checks`
folders; only synthetic fixtures were used. The integrated native/UI receipt and
installer verification are recorded by the 067G delivery workflow.
