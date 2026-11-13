# Revision review and guided repair — 067G

Addresses **G.02**, **B.02** and **B.03**. Broader milestones remain **Parcial**.

`design_review` offers three revision-bound actions:

- `compare`: choose `before_id` from `history_info`; `after_id` defaults to
  `current` or names another retained journal state. Both states are reconstructed
  and evaluated privately. Reports distinguish feature additions/removals,
  authored expression/reference/metadata changes, driven geometry changes,
  named dimensions, assembly authoring and each revision's physical BOM.
- `diagnose`: evaluates private authoring and returns failed features, their
  kernel/evaluation errors, current expressions, missing inputs, earlier
  dependency choices and directly dependent features. The source SHA-256 binds
  a correction to the inspected document. It also exposes retained rejected
  single `feature.edit` operations at the current revision, including attempted
  dimensions/references. This handles ordinary atomic edit rejection while the
  current model remains valid; `failure_id` identifies that retained attempt.
- `prepare_repair`: supply `id`, the inspected `source_sha256`, and an explicit
  `feature.edit` command correcting `p`, `native` or `inputs`. It creates an
  ordinary `doc_preview` candidate. A separate revision-checked `doc_apply`
  publishes the reviewed candidate. Invalid whole-document geometry rejects.
  For a rejected operation, also supply its `failure_id`; the retained attempt
  must belong to the same current revision and target exactly this feature.

Every action requires `expected_revision`. Unknown revisions, stale sources,
deletion, suppression and unrelated feature edits reject. There is no automatic
reference substitution, tolerance inflation, discarded solid or silent rebase.
Repairs involving other operations must use the normal explicit authoring tools.
Multi-command rejected transactions remain visible in `rejected_operations`,
but are not reduced to a single-feature correction. Expired or older-revision
failure records cannot authorize a new repair. Changed tolerance/fuzzy fields
are rejected recursively, including inside native recipes.

Studio exposes **Compare revisions**, **Repair failed feature** and **Hide review
overlay**. Comparison highlights before geometry in orange and after geometry
in cyan, with a selectable report window. Repair shows explicit editable
dimensions and reference/dependency recipes, then a checked candidate review
before Apply. Overlays invalidate on any revision or document change.

Geometry comparison uses native B-rep fingerprints when present. Other geometry
is explicitly labelled `display_mesh_only`; tessellation is never production
authority. The overlay is derived wire geometry, not a geometric Boolean
difference or manufacturing qualification. Assembly quantities include the same
physical occurrences as the BOM contract. Only retained history can be compared;
this feature does not retrieve archived or external revisions or establish
distributed collaboration.

Validation source: `tests/design_review_tests.cpp` covers driven native geometry,
named dimensions, assembly BOM quantities, no-mutation comparison, unknown/stale
revision rejection, failed-source diagnosis, retained repair preview, forbidden
suppression/tolerance changes, source fingerprint rejection, ordinary rejected
edits against a valid design and one atomic repair publication.
Executed results and UI captures are recorded in the iteration receipt.
