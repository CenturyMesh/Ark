# Background authoring — 051A

The Studio authoring path prepares ordinary geometry-changing `doc_apply`
requests on a private document snapshot. This covers feature editors, parameter
HUD rebuilds and ordinary `doc_apply` requests queued through Studio's MCP and
Assistant bridge. Metadata-only edits and already prepared candidates keep their
existing paths. Other operations retain their existing execution contracts.

The evaluator has no live-session, window or graphics context access. Its copy
has no shared activity ledger or pending saved-native-geometry custody. Original
commands and checks execute on the private candidate, with progress from the
normal evaluation observer. A successful result must supply an existing
`PreparedTransfer` within the existing 128 MiB candidate allocation limit. There
is no fallback that recomputes against a newer drawing or reports an uncached
candidate as committed.

Studio retains synchronous editor completion semantics while pumping a live
render of the committed scene. During preparation, middle-button orbit/pan,
wheel zoom and body selection operate on that scene. Editing controls are
temporarily held, incoming external calls queue, and authored animation/direct
editing actions do not run from the rendering loop. **Cancel (Esc)** cancels
publication immediately. Closing the window cancels preparation before normal
close/save handling. This preparation state is separate from the existing busy
mode whose worker owns the live session; that older mode still renders only its
captured frame and does not enable navigation.

The host publishes once, on the session-owning thread. It checks both captured
revision and document epoch, including a replacement drawing with identical
bytes and the same public revision. Prepared transfer adoption also checks the
existing authoring fingerprint. Queued Studio requests retain their arrival
epoch and their explicit expected revision (or an arrival revision when the
host API omitted one). They cannot silently apply against the preceding
request's newer state.

The host-only completion helper feeds the original request through normal
`AgentSession::call` transaction, actor/motivation, journal-capacity, failure
capture and usage-preservation rules. Dispatch consumes the prepared candidate;
the journal still describes the original commands and the checks already ran on
that candidate. Publication preserves usage and activity received after capture.
Failure to append history rolls the prepared geometry back. A stale, cancelled,
invalid or over-capacity result remains a failed result.

Cancellation uses the existing native RPC cancellation scope with its existing
request limit. Evaluation-pool tasks capture that scope when scheduled, so their
native requests inherit cancellation and the request limit. CPU work without an
interrupt point may still finish privately. Its cancelled generation cannot
publish. The persistent worker joins at shutdown, so closing or the next queued
edit can wait for that remaining private work to finish.
There are no new agent turn/token budgets, geometry tolerances or silent rebases.

Local verification:

- `async-authoring-051a` exercises isolation while blocked, original journal
  commands/provenance, newer usage/activity, caller checks, invalid geometry,
  recipe mismatch, same-revision replacement epoch, concurrent edits, immediate
  cancel (including a ready result cancelled at publication), rejection with
  history suppression, malformed arguments, no-op preservation of ordinary and
  compacted journals/undo/redo, journal-capacity failure after successful
  preparation, parameter-driven evaluated geometry, and shutdown.
- `studio-async-authoring-ui` drives actual queued window messages during a HUD
  rebuild, checks orbit/zoom/selection and unchanged live authoring before
  publication, captures the live scene, and checks successful geometry, a stale
  external queued request, Escape, same-revision replacement and close cancellation.

The portable worker target passed locally on the isolated build. Both Studio
async-authoring and history-compaction selftests exited successfully with fresh,
revision-guarded synthetic drawings. Inspected captures show live orbit/zoom and
selection during preparation, the progress and Cancel control, published geometry,
and the retained drawing during cancellation. These are local runtime/UI checks,
not provider latency, deployment or manufacturing qualification evidence. Native
pool cancellation has its separate evaluation-schedule test. The integrated
051A core and patched-native worker suites also passed, followed by native
Studio async-authoring and history-compaction selftests (exit 0). The final
native captures confirm navigation/selection, publication and cancellation on
that build; the initial isolated capture build used the portable backend.
