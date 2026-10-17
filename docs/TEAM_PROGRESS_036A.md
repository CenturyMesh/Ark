# Team progress evidence - 036A

A.04/D.02/D.04: semantic progress and diagnostic accounting are separate.
Repeated document/team reads can consume provider input without authoring a
sketch, feature or assembly. A changing token counter or provider/tool status
does not establish design progress.

`team_context{}` and `team_context{detail:"progress"}` return:

- `response_scope:"team_progress"`, `changed`, `evidence_state` and a per-caller
  `evidence_version`;
- actual task/mail/error/peer-state evidence and measured committed team steps;
- the last document revision observed in an executor receipt, explicitly marked
  as observed rather than a fresh document inspection;
- `next_action`, including authoring the next coherent editable checkpoint,
  resolving an actual blocker, finishing a reported turn or yielding after a
  Focused handoff.

The first read establishes that caller's baseline. Repeated reads with the same
semantic evidence report `no_new_evidence`. Per-request status, token metrics
and lease countdown are excluded from this comparison. Actual messages, tasks,
errors, commits and newly observed revisions remain visible. This does not
deduplicate user messages, hide errors or execute a model's proposed edits.

Use `team_context{detail:"diagnostics"}` when investigating request bytes,
provider accounting or a transient status. This explicitly restores
`roles[].status`, `token_evidence`, `request_context` and lease countdown.
It supersedes the unconditional metrics presentation described in 033D.
Diagnostics do not change the semantic baseline merely because their counters
changed. Original provider usage and action receipts remain retained.

`team_send` acknowledges actual recipients as `active` or `queued`; delivery
does not prove completion. In Focused mode the sender must finish its turn so
the queued recipient can run. In Parallel mode active recipients consume mail
at their next request boundary, and queued recipients need an available worker.

Focused remains the default. No new provider-round, turn or token execution
budget, automatic cancellation or automatic geometry write is introduced.
Synthetic regressions verify the host contract and a real atomic CAD executor
write. They do not establish real-model design quality, elimination of every
polling loop or a matched reduction in provider tokens/cost.
