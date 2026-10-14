# Active Assistant and team steering — 024B

Roadmap scope: **D.02 Parcial**, **D.03 Parcial** and **A.03 Parcial**. This
change covers local message delivery, retained evidence and execution guards.
It does not establish design quality, cost savings, hosted integration or
production qualification.

## User interaction

While the single Assistant is working, **Queue** sends a new decision to that
same conversation. The existing image composer remains available during a
provider wait: attach PNG/JPEG, choose **Inspire design** or **Image to CAD**, and
optionally supply rough dimensions. Queue snapshots the actual local pixels,
intent, dimensions and text. The next provider request consumes accepted
messages in order. A later attachment does not replace a reference already
accepted into the conversation.

Teams retain their selected recipient behavior: Focused selects the lead by
default; Parallel broadcasts by default; an explicit role targets only that
role; `all` broadcasts. An explicit user broadcast accepts every addressed
recipient or none if a configured recipient/mailbox limit blocks delivery.
Connector capability and request preflight reject unsupported or oversized
image histories explicitly; ARK never substitutes a filename for pixels.

The final response waits for accepted steering. A tool-free response that
arrives after a new message is retained as historical output, but its outdated
final text is withheld and the provider receives the new input first. New input
arriving during the host completion check also prevents finalization. A Send in
the short interval between worker completion and final UI delivery is retained
as an immutable queued message and starts a fresh turn after the final history
is applied. Such a message retains the original transport and drawing epoch.

Cancellation prevents another image upload and rejects further steering.
Already accepted pending references remain in retained conversation evidence;
they are not interpreted as completed work. Pending controls and the deferred
final-delivery queue are cleared by the explicit Cancel action. User references
are conversation data, not `.ark` geometry or installer assets.

## Execution contract

`AssistantSteering` validates accepted messages, retains their FIFO order and
tracks a monotonically increasing version for one run. Inbox consumption binds
that version to the next provider request. The host captures the request version
onto queued UI tool work and checks it again immediately before execution.
`AgentTeam::requestVersion` and `requestCurrent` provide the corresponding
per-role guard. Unknown tools require the write guard unless their advertised
`readOnlyHint` explicitly permits reading.

An old write rejected after steering returns a real error receipt with no tool
execution. Existing `expected_revision` checks remain required; input versions
never rebase commands or bypass atomic publication. Geometry that has already
entered the executor is allowed to finish. The existing kernel progress overlay
keeps document editing blocked until that atomic operation returns; this scope
does not interrupt or read a partially evaluated model.

The addressed role also cannot deliver a downstream `team_send` assignment
from its old request. Delivery checks the sender's input version under the
mailbox mutex, preventing targeted lead steering from waking an unaddressed
specialist to execute an obsolete design task. Passive messages/reports retain
their existing behavior as historical evidence, rather than new assignments.

Eligible pre-output HTTP retries consume the latest pending input before the
next provider request. Retry counts and transport policy remain unchanged.
Earlier tool receipts are retained, and a retry never replays committed tools.
No turn or token budget was introduced.

Local preflight may run while the current provider response still has an
unanswered tool batch. Only that incomplete call is excluded from the temporary
validation copy. Actual retained history is unchanged, and no successful receipt
is invented. Accepted references are also considered together with prior and
pending reference pixels when validating the outgoing request bound.

## Inline snapshots during cooperative writes

`snapshot` without `path` is read-only document inspection, including for the
read-only reviewer. A peer's cooperative write reservation does not block that
inline inspection. Provider history receives actual image content when the host
returns a valid image and revision receipt. Snapshot pixels remain display
evidence; B-rep and semantic references determine production geometry.

A snapshot with an explicit `path` remains a file-write operation: read-only
roles cannot execute it, stale input blocks it, and another role's write window
still guards it. The standalone tool keeps its conservative `readOnlyHint:false`
because one invocation can export a file. `image_asset` action `inspect` is also
classified as read-only; image import/removal remains guarded.

## Verification scope

`assistant_steering_tests.inl` uses synthetic pixels and injected offline
transports to check ordered text/image delivery, rejected stale writes,
tool-free pending final replies, input during completion checks, fresh input on
pre-output HTTP retry, unsupported image capability, unpaired live chat
preflight, atomic final close, and retained pending references on cancellation.

`agent_team_steering_tests.inl` checks in-flight image steering, captured host
tokens, no false goal completion, final-response continuation and atomic
broadcast rejection. Targeted lead steering rejects a stale downstream
assignment without waking or writing through an unaddressed specialist; passive
evidence delivery remains available. Its cooperative-window fixture verifies inline snapshot
inspection by a writer and a read-only reviewer while file exports remain
blocked. The fixture returns synthetic PNG pixels and real host routing receipts;
it is not evidence of live provider vision or geometric verification.

Studio's `--self-test-assistant-steering-ui` drives the rendered Queue, intent
and Cancel actions against offline transports. It holds an old write in the UI
queue, attaches real generated pixels, checks stale rejection immediately
before UI execution, queues another image before a final response, tests the
worker/final-event delivery gap, and retains an accepted image after Cancel.
Captures `.steer-active-image.png` and `.steer-queued-image.png` record the affected
composer controls. Run at both desktop and laptop sizes and inspect captures.
The iteration verification receipt records the executed binaries, check results
and any remaining limits; these fixtures alone make no live-provider claim.

Local verification on 2026-10-14, using the native Release build:

| Check | Observed result |
| --- | --- |
| `assistant-connector` | Passed, including ordered pixels, stale writes, finalization and cancellation fixtures. |
| `agent-team` | Passed after the targeted-lead downstream-assignment regression was added. |
| `studio-assistant-steering-ui` | Passed at 1440×900. |
| `studio-assistant-steering-ui-compact` | Passed at 1366×800. |
| Capture inspection | Actual native Width field shows `140`; blank Height/Depth, native composer text, thumbnail, Queue/Cancel and intent controls are visible without clipping at both sizes. |

The UI fixture initially attempted to fill a dimension control before its
post-toggle paint created that native control. Its correction redraws after the
rendered click and synchronizes on the completed request inspection, rather than
transport entry. Failure cleanup also joins the live worker before fixture
captures leave scope. Final successful checks use the corrected fixture.
