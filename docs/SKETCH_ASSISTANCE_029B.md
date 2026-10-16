# Sketch assistance and native accessibility in 029B

Milestones **B.01** and **B.02** remain **Parcial**. The bounded local scope below
does not establish universal sketch solvability or accessibility qualification.

## Constraint review

`sketch_repair` is a read-only query. Supply `name`, the observed
`expected_revision`, and optionally the proposed `sketch` source. The response
identifies the source with `source_sha256`, reports its revision, and lists
alternative single-constraint deletions. Each alternative contains an ordinary
`sketch.set` command, the original constraint index/ID, the measured residual and
remaining local degrees of freedom. Generation does not alter the document,
coordinates, targets or retained expressions.

Review the removed relation and its design intent before selecting an alternative.
Pass its commands and observed revision to `doc_preview`, inspect the full-document
result, then explicitly accept through the returned `doc_apply` recipe. Rejecting
or closing review performs no write. A stale revision or stale prepared candidate
rejects. The source recipe retains the caller's numeric seeds and expression
strings; the numerical search runs on private evaluated copies.

The search tries deletion of each one constraint, with at most 128 constraints
and 256 free coordinates. An already satisfiable sketch produces no deletions.
If no individual deletion solves the sketch, `no_single_deletion_repair` exposes
that limit; the query does not silently remove additional constraints. This is
minimal in the tested one-deletion search only: failed nonlinear convergence
does not prove global infeasibility, a minimal conflict set, or unique design
intent. Producer-owned sketches and unsupported projected/offset geometry require
review of their source. Invalid expressions are errors, not repair candidates.

## Numerical solve

The solver differentiates supported residuals analytically with forward-mode
automatic differentiation. It equilibrates residual rows and variable columns,
then solves the damped augmented system with Householder QR per connected
component. This avoids forming normal equations and their squared conditioning.
Trial weights are fixed during each acceptance comparison. Final validity still
uses the original unscaled residuals and the existing `1e-6` acceptance bound;
repair requests cannot enlarge it. Driven coordinates remain excluded from the
free variables. Collapsed lines in angular, parallel, perpendicular and tangent
constraints fail explicitly.

After row normalization, column damping retains a unit coordinate metric so a
nearly null derivative cannot amplify roundoff into an arbitrary translation.
Trials outside the existing finite coordinate domain reject without clamping.
The full modeling workflow exposed this regression in a free arc center tangent
to a fixed line: the initial equilibration could move the unconstrained center
coordinate beyond its permitted bounds. The corrected solver is checked against
that original workflow and translated/rotated versions of the same null direction.
The residual acceptance tolerance and coordinate bounds are unchanged.

Local regression fixtures include mixed angular/distance constraints at lengths
`1e-4`, `1` and `1e6`, preserved driven expressions, contradictory targets,
degenerate-line rejection, and the bounded repair search. These are numerical
tests of supported constraints, not manufacturing qualification or universal
nonlinear-solver guarantees.

## Native forms

Native edit, checkbox and combo controls publish their visible field label as
their MSAA accessible name. Keyboard focus indicators are visible, Tab can enter
and leave the scrolling field viewport, and Page Up/Page Down and wheel navigation
retain values. Apply and Cancel stay outside the scrolling content.

The native form handles DPI and work-area change messages by resizing existing
controls and fonts, preserving values/focus and revealing the focused field.
Two-column forms collapse to one column when the available width requires it.
The application requests per-monitor-v2 DPI awareness when the Windows API is
available, with a compatibility fallback.

The automated native-form fixture checks accessible names through `IAccessible`,
focus state, retained edits, clipping, cancellation, and injected DPI changes
between 96, 144 and 192. Injected messages exercise the real layout handler;
they do not qualify physical cross-monitor transitions, Narrator speech output,
third-party screen readers, every specialized dialog or arbitrary display sizes.
Visual screenshots and integrated UI execution are recorded in the iteration's
verification evidence.

## Verification entry points

- `ark-sketch-diagnostics-tests`: numerical scales, constraints and read-only
  proposal semantics.
- `ark-sketch-repair-tool-tests`: source preservation, revision rejection,
  full-document preview, explicit prepared acceptance and unsupported cases.
- `ark-studio --self-test-editor-scroll-ui`: native accessibility queries,
  keyboard/scroll controls, DPI/work-area message handling and form screenshots.
- `ark-studio --self-test-authoring-assistance-ui`: both sketch review Cancel
  paths, an intervening revision during review, explicit Preview/Apply buttons,
  retained expression source, surface-orientation authoring and feature reopen.
  Captures show the actual native forms; automated hidden-window execution is
  not a human usability study.
- `ark-studio --self-test-scene-cache-ui`: native edit, reorder, occurrence
  placement, undo/redo, appearance, display mutation, normals, document reload,
  GPU release and semantic picking against a forced full rebuild.

Source formats are unchanged by this assistance. No manufacturing drawing is
created by diagnosis, proposal generation or native-form checks.
