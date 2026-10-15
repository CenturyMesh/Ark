# Configurations and selective loading in Studio

029B adds Manage → Configurations for B.04. The same workspace is available from
the document menu. It edits retained assembly recipes through `doc_apply` with
the observed document revision and rejects changes made while a form was open.
The existing Drawings workspace and drawing viewport remain the technical-sheet
authoring path; this workspace does not create drawings.

Choose **Create snapshot** to capture current parameter expressions and occurrence
visibility. The second form names the recipe and can override one parameter and
one occurrence. Saving the recipe does not apply it. Choose **Apply** to publish
its retained overrides, **Edit recipe** to revise overrides, or **Remove** to remove
the recipe while retaining the current applied design. Reopen Edit recipe to
change additional entries. Expressions must resolve; invalid edits reject.

**Load subtree** selects an explicit assembly node for evaluation and retains the
dependencies required by the supported assembly/mate rules. Definitions outside
that loading scope remain authored source. **Load all** restores full evaluation.
Selective loading and visibility are distinct: a hidden occurrence can remain
loaded, while an unloaded definition has no current evaluated body.

Recipes, loading roots and stable occurrence identities persist in the document.
Undo uses the same transaction history as other source edits. Existing exact
geometry, drawing staleness and source-pin checks still apply; a configuration
does not establish manufacturing or external-reference qualification.

The synthetic `--self-test-configuration-workspace-ui` workflow exercises capture,
apply, parameter and visibility overrides, subtree/load-all behavior, recipe edit,
invalid-expression rejection, save/load and removal. Its screenshot output also
checks access to the new entry alongside the retained Manage controls.

The fresh 029B native configuration UI check passed in 4.03 seconds. Captures of
the recipe form and final Manage workspace were inspected at 1280×800 with the
window reporting 144 DPI. Current Drawings and the viewport dock remain visible;
their retained native workflows passed separately. See the local receipts in
[the authoring foundation](CAD_AUTHORING_FOUNDATION.md#fresh-local-studio-verification).

Isolated container v2 retains scoped configurations and feature-owned pins with
explicit editable/readonly ownership. Its load selection stays local to the
workspace and never overwrites the parent's selector. Mixed-boundary recipes,
readonly writes and global history/Skeleton boundaries reject explicitly.
Owning-document persistence retains these constructs alongside embedded images,
Stamps and canonical drawing sources. See [assembly scaling](ASSEMBLY_SCALING_029B.md).
