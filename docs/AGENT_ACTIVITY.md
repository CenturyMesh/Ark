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

Agents must submit `tool_feedback` before completing their work. Each actual
activity ID receives utility and difficulty scores from 1 to 5, a description
of the action, and optional `issue`, `wished_tool` and `wished_options` text.
Utility 1 means unhelpful and 5 effective; difficulty 1 means easy and 5 very
difficult. These are subjective self-reports, separate from measured results.
The host never supplies scores for production agents. A cancelled/interrupted
agent leaves visible pending records; it does not fabricate feedback.

Ratings are immutable, confined to the owning role, and may be submitted in
batches of 64. A changed model within the same role is identified as a
same-role review. Audit tools themselves are excluded to avoid recursive
feedback. No turn or token budget is introduced. The ledger retains up to
4096 actions, then reports an explicit capture gap and increments
`unrecorded_actions`; existing document size limits also apply. These are
storage limits, not agent execution quotas. Scores alone do not qualify
geometry, manufacturing, tool quality or model performance.

Documents containing this ledger use schema 33. Older documents remain
readable; the application version alone does not migrate them. Older ARK
versions cannot open a newly saved v33 document. There is no MeshWorks
adapter or automatic remote publication in this iteration.

Live sketches are uncommitted overlays for received tool steps, including
new ordinary construction planes and parameter values. Native geometry is
published atomically once the step validates. Provider thinking produces no
CAD entities to display. A newly created plane supported on not-yet-evaluated
native topology cannot be previewed by guessing its frame. During evaluation,
the last committed scene remains visible with role and feature progress.
