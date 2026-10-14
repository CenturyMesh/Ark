# Cooperative AgentTeam write windows

Scope: **D.02**, in-app AgentTeam coordination. This is local team integration
evidence, not a production concurrency or manufacturing qualification claim.
Studio keeps Focused as its default; Parallel remains an explicit option.
Reservations are optional and add no provider-round, turn or token budgets.

The shared executor already serializes individual calls. That prevents two
callbacks from executing simultaneously, but leaves another role free to commit
between an author's inspection/preview and its later revision-bound write. The
printer authoring audit observed two `doc_apply` conflicts and one `doc_preview`
conflict. A reservation can protect that short gap within one AgentTeam.

Only the team's provider tool schemas add `team_reserve_write` and
`team_release_write`; standalone MCP discovery remains unchanged.

An author first calls `doc_info` successfully and retains its observed revision.
It can then request a window:

```json
{"expected_revision":42,"lease_ms":10000,"reason":"Preview and commit the corrected mounting bracket"}
```

`team_reserve_write` requires a writable role and the same revision as that
role's successful inspection. Under the executor lock it performs a real
read-only `doc_info` verification. A changed revision rejects the grant; the
reply reports the current revision without changing the requested baseline.
The successful result includes `reservation_id`, owner, expected revision,
remaining lease and the actual revision verification receipt. This additional
verification is a real read, not an assumed revision or a CAD mutation.

`lease_ms` defaults to 10000 and accepts integers from 100 to 30000. This bounds
only the cooperative reservation. It does not bound a design goal, conversation
or native kernel operation. The owner may explicitly renew with another reserve
request after inspecting the current revision. A renewed live window keeps its
token; a newly acquired window gets a new token.

While the window is live, other roles can inspect geometry, read `doc_info`,
preview, communicate and store proposals. Every peer executor mutation is
rejected at the final serialized gate with `error_code: "team_write_busy"`,
owner, reservation ID, baseline and remaining lease. The rejected operation
does not reach the host executor. This covers direct `doc_apply`,
`library_insert`, replacement/import/history operations, other tools advertised
as writes and `team_apply_step` commits from commands, proposals or prepared
candidates. Normal role permissions, command limits and candidate ownership
remain in force for the owner.

The owner releases promptly:

```json
{"reservation_id":"write-window-1"}
```

Only the owner can release that active token. Missing, expired and previously
released tokens reject. `team_context.write_reservation` exposes the actual
active owner and remaining lease. Time uses a monotonic clock and expiry is
checked during coordination and immediately before writes. An executing native
operation retains the existing executor lock until it returns; expiry does not
interrupt it or expose partial geometry.

A window also clears on owner turn exit, accepted task finish (`completed` or
`needs_input`), accepted goal completion, cancellation and team replacement.
Provider failure, interruption or an explicitly configured yield reaches the
same owner turn cleanup. A paused or stalled provider cannot hold peers beyond
the lease once the team is resumed. Release/expiry never automatically retries
a rejected tool, changes `expected_revision`, discards retained previews or
clears unresolved tool errors. A peer must inspect the new revision and decide
on a corrected edit; successful corrected operations retain the existing error
resolution rules.

The reservation covers **one AgentTeam's shared executor**. UI edits, another
team/session or external clients are outside that cooperative state. Their
changes can still make an owner's edit stale, and the host's atomic
`expected_revision` check continues to reject it. Reservations are neither
document locks nor enforced part ownership.

Verification lives in `tests/agent_team_write_window_tests.inl`, included by the
existing `ark-agent-team-tests` target. Deterministic two-role barriers check
contention, non-owner release rejection, direct/library/step/proposal commit
gates, reads during a window, stale grant rejection, retained errors, explicit
release, owner exit, lease expiry, task finish, cancellation and restart. The
full AgentTeam test executable passed locally with these cases. No comparable
before/after printer benchmark has yet established a cost or conflict-rate
improvement; failed attempts remain part of the audit.
