# Project tool activity

012D records host-observed agent actions in the project's existing `.ark`
container. Open **All tools → View & agents → Project tool activity** to read
the log. `activity_info` provides paginated JSON, including pending feedback
and an optional `actor_id` filter. Save the project to persist new records;
the unsaved indicator includes activity changes even without geometry edits.

Each record includes actor/role/model, UTC time, tool, a bounded command
summary, revisions, outcome and host elapsed milliseconds. Core call time
excludes provider and UI queue wait; coordination entries identify their wider
timing scope. Full prompts, images, arguments and B-reps are not copied into
the log. Failed attempts are retained. Undo and iteration restore preserve it.

033D also retains bounded numeric request-context snapshots on successful
`team_context` activity records. Per-role counters separate serialized schemas,
instructions, original history, messages and tool results. These are measurements
at that observation, not complete provider transcripts or token estimates.
Unavailable measurements remain explicit; an oversized snapshot is marked
`request_context_omitted`. Saving retains these optional fields in the existing
activity schema without changing document identity/version requirements.

Agents must submit `tool_feedback` before completing their work. Each actual
activity ID receives utility and difficulty scores from 1 to 5, a description
of the action, and optional `issue`, `wished_tool` and `wished_options` text.
Utility 1 means unhelpful and 5 effective; difficulty 1 means easy and 5 very
difficult. These are subjective self-reports, separate from measured results.
The host never supplies scores for production agents. A cancelled/interrupted
agent, or a run that ends with feedback still missing, leaves visible pending
records; it does not fabricate feedback.

Ratings are immutable, confined to the owning role, and may be submitted in
batches of 64. A batch is atomic: every item is validated and nothing is
recorded unless all pass. Since 076A a rejection names each offending item by
index and `activity_id` with its reason (not found, another actor, already
rated, repeated in the batch, invalid score or text) and returns
`rejected_ratings`, `already_rated_ids` and the current pending IDs, so the
agent can correct the batch in one retry. Atomic batches are kept because a
team report's ratings must record nothing when any item fails (042D). A changed
model within the same role is identified as a same-role review. Audit tools
themselves are excluded to avoid recursive feedback. No turn or token budget is
introduced. The ledger retains up to 4096 actions, then reports an explicit
capture gap and increments `unrecorded_actions`; existing document size limits
also apply. These are storage limits, not agent execution quotas. Scores alone
do not qualify geometry, manufacturing, tool quality or model performance.

Since 042D a team role can put the same rating items in the optional `ratings`
field of `team_report_task` or `team_finish_goal` instead of calling
`tool_feedback` in a separate provider round. The host first runs the report's
own checks with those IDs counted as rated, then records the ratings through
the role's audited executor, which validates every item before applying any.
An invalid, duplicate or partial set rejects the report explicitly, lists the
remaining `unrated_activity_ids` and records nothing. The accepted receipt
returns `rated_ids`. If a teammate changes the team state while the ratings are
being recorded, the report can still be rejected afterwards; that rejection
states that the ratings were recorded. `tool_feedback` remains the route for
the single assistant, ACP and external MCP clients.

### Run-scoped completion gate (076A)

Receipts carry `pending_feedback_ids`, newest first and at most 64, plus the
untruncated `pending_feedback_count` for the calling actor. The Single
assistant uses one actor id (`assistant`) for every conversation and provider,
so this list can include rows that earlier conversations, other providers or
interrupted runs left unrated. Those rows are backlog: they stay pending in the
ledger, can be inspected with `activity_info` and `pending_only`, and may be
rated only by an agent that can describe the actual action.

The HTTP, ACP and Claude Code connectors gate the end of a run on the activity
IDs that this run's receipts created. Retained history is not replayed into the
gate. The reminder lists exactly the still-unrated actions of this run,
including reads made after an earlier reminder, and states the backlog count.
Verification reads that team tools perform for a role (`revision_verification`,
`initial_checkpoint_verification`) count as actions of its run.
Another reminder follows only when the run's count of unrated actions is lower
than at the previous reminder. Ratings of rows outside the run never lower it
and every new action raises it, so a model that rates the listed IDs and makes
one new read in every round receives a bounded number of reminders; this is a
progress rule, not a turn or token budget. A reminder round that does not lower
the count ends the run without an error: the final reply is accepted and
shown (after the reply that triggered the first reminder, when they differ),
followed by the note *ARK: tool feedback is still pending for N actions of this
run*. A non-error status reports the same count during the run, and Studio's
final Single status reads **Feedback pending** with that count instead of
**Reply ready**. The rows remain pending and nothing is rated by the host.
`AssistantResult::feedback` records the run's action, rated and pending counts,
the pending IDs, the backlog and the number of reminders. A project boundary
(`doc_new`, `doc_load` or a changed project id) removes the earlier IDs from the
gate; they are counted in `run_boundary_dropped` (with up to 64
`run_boundary_dropped_ids`) and the state is `boundary_dropped`, never
`complete`, so `run_actions` = `run_rated` + `run_pending` +
`run_boundary_dropped`. An `ARK_FEEDBACK_PENDING_V1` developer record keeps
that summary in the conversation; later Single runs treat those rows as backlog.

A team role is gated only on actions it performed in its own team run.
`team_report_task` and `team_finish_goal` with state `completed` still require
those ratings, as described above; backlog from earlier conversations neither
blocks nor is listed in `unrated_activity_ids`, which now comes with an
untruncated `unrated_activity_count`. A team role's turn that ends with its own
actions unrated is no longer a failed turn. Instead those actions keep blocking:
the role's status and reply say so, its snapshot carries `feedback` (the turn's
outcome) and `pendingFeedback`, the lead's handoff notice counts them, and
`team_finish_goal` with state `completed` is rejected with
`teammate_unrated_feedback` while any idle teammate holds unrated actions of
this team run. A completed goal declared earlier is shown as needing input
while such actions remain. Only the owning role can rate them, through
`tool_feedback` or its report's ratings. The gate adds no turn or token budget.

Since 076A Studio also declares `actor.run`, a per-run identifier, on the
Single assistant's and each team run's actions. A rating whose rater declares a
run is recorded with provenance `same_actor_other_run` when the rated row comes
from another run or predates the field; ledgers and actors without `run` load
and rate exactly as before (`agent_self_report` or
`same_role_review_with_changed_model`). Such ratings never count as progress of
the current run.

045A extends the ledger to declared clients (`actor.kind:"client"`), which is
how Studio's live MCP bridge identifies every call (`id:"live_mcp"`). For a
client, only committed authoring and failed calls are recorded: a rejected
call, a `query_batch` with a failed row or a rejected preview. Successful reads
are not, so external sessions do not fill the 4096-row ledger or the 4 MiB
document with inspections. Client records require feedback like agent records
and are rated with `tool_feedback` under the same actor. Pending client
feedback belongs to that actor only; it does not block a team role's
completion. Human Studio edits remain outside the ledger.

Documents containing this ledger use schema 33. Older documents remain
readable; the application version alone does not migrate them. Older ARK
versions cannot open a newly saved v33 document. There is no external
adapter or automatic remote publication in this iteration.

Live sketches are uncommitted overlays for received tool steps, including
new ordinary construction planes and parameter values. Native geometry is
published atomically once the step validates. Provider thinking produces no
CAD entities to display. A newly created plane supported on not-yet-evaluated
native topology cannot be previewed by guessing its frame. During evaluation,
the last committed scene remains visible with role and feature progress.
