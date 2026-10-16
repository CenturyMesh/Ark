# 031B: explicit refresh of independent prepared proposals

Milestones: A.03, D.02, D.04. The global document revision still protects every
commit. Ordinary commits now retain other prepared recipes within the existing
cache bounds, so an agent can explicitly refresh an independent proposal without
reconstructing or resending its command batch.

For example, agents A and B preview different parts at revision 12. A commits,
producing revision 13. B's old apply payload rejects. B reads revision 13 and
requests:

```json
{"candidate_id":"B-original-candidate","expected_revision":13,"detail":"changes"}
```

This is a `doc_preview` call. If its scope is unchanged, it evaluates the retained
recipe against the current document and returns a new candidate ID, recipe,
before/after evidence and `apply` payload. B inspects that result and explicitly
commits the returned payload. A's changes remain present. Refresh never publishes
automatically, and cached apply still checks the complete current baseline.

New feature IDs come from the current document allocator. Agents must use the
new receipt's IDs. References between additions in a batch use unique names;
provisional numeric IDs are not eligible for refresh. Missing or ambiguous names
reject. The preview response reports `refresh_supported` and, when unavailable,
`refresh_reason`. A refresh receipt identifies its source candidate/revision and
the current revision.

The initial refresh domain is recipes containing only audited `feature.add` and
`feature.edit` operations. Scope includes existing edit targets, transitive
feature inputs, selector bindings, tessellation settings and conservative
document authoring context. Parameters, sketches, datum definitions, assembly,
materials and other non-feature authoring are compared conservatively. When a
target or an ancestor consumes named sketch/datum geometry, the scope retains
all feature definitions until the complete implicit read graph is audited;
unrelated feature changes can therefore still conflict in that domain.
Unsupported feature types, implicit dependencies and recipes with postconditions require a fresh
explicit command preview. Parameter removal (`p_remove`) and repeated edits of
the same existing feature in one recipe also require a fresh command preview;
their cumulative operation changes are outside the initial audit. This is not a
general merge of arbitrary edits.

Changed targets/dependencies or context return an explicit refresh conflict.
Unsupported recipes return an explicit unsupported-refresh error. Neither
changes the document or commits a valid subset of the batch. No tolerance is
relaxed and no rejected solid is discarded. The caller can inspect the current
design and prepare a revised recipe when necessary.

Studio team receipts remain bound to the originating role. Refresh passes the
same role, command-count, steering and discipline gates as the retained recipe;
another role cannot adopt it. Ordinary peer commits retain ownership receipts,
while successful apply retires the consumed candidate. Document replacement
clears custody, including queued calls from an earlier project. Session undo/redo
also clears prepared candidates. Existing cache limits remain four candidates,
an estimated 128 MiB total and five-minute expiry; eviction still requires a new
preview. Neither retention nor refresh is a reservation.

Synthetic regression coverage is in `candidate_refresh_031b_tests.cpp` and
`agent_team_candidate_refresh_031b_tests.inl`: independent publication, ordinary
peer edits, fresh allocation, named batch references, stale apply rejection,
target/transitive/context conflicts, project boundaries, cache eviction and
role ownership. The integration tests exercise the shared document session.
The iteration verification receipt records executed suites and limits.

This reduces the need to resend a retained recipe after an independent commit.
It does not refund tokens already used, salvage partial failed transactions or
establish measured provider token savings. Refresh can evaluate geometry; it
uses the audited incremental evaluator, and a subsequent cached apply reuses
the validated result. Focused remains the default; Parallel remains optional.
