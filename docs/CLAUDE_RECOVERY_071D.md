# Claude Code terminal results and recovery — 071D

**A.02/B.01/D.02/F.03 — Verificado no escopo local**, with verification in
[071D](iterations/071D.md).

The Claude SDK's `result` message can have `subtype:success` together with
`is_error:true`. The subtype identifies the envelope; it does not override a
reported error. The old adapter correctly stopped that turn but discarded its
reason and displayed the confusing phrase `result success`.

071D classifies known terminal reasons, API statuses and bounded recognised
error patterns. The error card distinguishes expired authentication, other
authentication errors, context limits, rate/account limits, connection/TLS
failures, model/image errors and provider service errors. Only categorical
metadata is retained; provider bodies, account identifiers and credentials are
not copied into error receipts. Unknown reasons remain explicit rather than
becoming successful completion. Genuine completed and legacy success envelopes
remain supported.

The official [SDK result contract](https://code.claude.com/docs/en/agent-sdk/typescript#sdkresultmessage)
defines the independent error flag and terminal metadata. The official
[authentication error reference](https://code.claude.com/docs/en/errors#oauth-token-revoked-or-expired)
describes login renewal when saved OAuth credentials expire.

## Observed diagnosis

An isolated test using the installed CLI, the selected model/effort and only a
synthetic read tool reproduced `success/is_error:true/api_error` without loading
any drawing or executing a CAD tool. Categorical signals identified expired
OAuth authentication. The official `claude auth login --claudeai` flow completed
successfully and the user confirmed normal operation afterward. No token was
edited/copied by ARK and no user drawing was modified. This establishes an
authentication failure independently of document age or a geometry cache limit.

The initial model-catalogue probe succeeded before sign-in renewal. Catalogue
availability alone therefore is not evidence that a model request will be
authenticated. Diagnostic probes send no prompt; actual model access is a
separate observation.

## Context continuity

The initial CLI prompt now honours the host's history-projection flag. After
projection or a real SDK `compact_boundary`, writes require a new successful
host `doc_info` receipt. Failed reads do not clear this requirement. Only bounded
compaction trigger/count metadata is recorded; provider summaries are not
promoted to authoritative history.

Existing committed receipts, user intent and original history remain intact.
Exact read references keep their complete source in the same transmitted
history and do not trigger an unnecessary fresh-read guard. No failed CAD call
is automatically replayed and no provider/account fallback is introduced.

## Human-facing connection

The assistant account button identifies the selected connector, so a previously
connected ChatGPT account no longer masks an active Claude Code route. The
reconnect action uses the user's installed, unmodified CLI and its official
login flow. ARK does not own or store the OAuth token. Reconnection does not
automatically resume a CAD task or repeat previous edits.

RAM and geometry workers are independent of the provider's authentication and
context limits. Increasing those local resources cannot renew an expired
provider credential or enlarge a remote model's context window.
