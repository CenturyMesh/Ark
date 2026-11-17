# 078B — bounded graph context and Obsidian navigation

**D.02 / D.05 / A.03 / A.04 — Em curso**, pending final native integration tests.

The live CAD graph, the typed Geometry Nodes view, the repository AST graph and
historical memory are separate sources. This work gives each a small, explicit
navigation contract instead of treating a truncated response as the whole model.
It makes no measured provider-cost or design-quality claim and adds no agent
turn/token execution budget.

## Live context

- `context_graph` preserves the existing schema and read-only behavior, with
  direction-aware, deterministic pages. Cursors bind source, project, revision
  and selection; stale or malformed continuations reject.
  The first agent-attributed graph read/batch reserves its audit identity before
  producing a scoped cursor, while still recording only the actual action once.
  Usage/feedback bookkeeping does not enter the saved-authoring source hash;
  ordinary audit recording therefore cannot invalidate its own continuation.
- Boundary edges and nodes remain when delta fingerprints omit unchanged facts.
  Incident edges include both directions even when node traversal is directed.
  Internal/boundary edges share a128-edge chunk; `next_call` continues those
  edges without repeating node facts before advancing the node slice. Wide
  boundaries truncated beyond64 nodes remain reachable through expansion and
  continuation. Navigation never bypasses `expected_revision` or native inspection.
- Graph construction indexes parameter names and incident edge indices rather
  than scanning every node for each expression token and every edge per node.
  The existing authored-data/incident-link fingerprint representation is retained.
- Request-history projection preserves distinct pagination/frontier receipts.
  There are no new live provider calls or inferred claims of better designs.

## Geometry-node discovery

`agent_onboard{topic:"geometry_nodes",detail:"guide"}` exposes the actual
`geometry_graph` tool through specialist discovery. The preloaded overview
points to that guide without adding a full specialist contract to every turn.
`inspect` accepts selected `ids`, a default16/max64 `limit`, and a current-source
cursor. It returns metadata/ports and a bounded incident-link excerpt, with
`context_call` for full adjacent discovery and `next_call` for remaining nodes.
Native source payloads and full parameter tables are absent from the overview.

The Studio canvas retains its complete compact snapshot. Agents use catalog and
selected context before preparing add/edit/link/disconnect/remove and publishing
the validated candidate. Direct socket admission distinguishes mesh/surface/solid;
it is not a whole-project type proof. Exact sampling into mesh remains explicit.
Team preparations use the same checked-recipe ownership, role operation scope,
step limits, source revision and project epoch guards as other prepared workflows.
Only the owning role can publish its retained node candidate. Reviewers receive
an inspect/catalog-only schema; backend permission checks still reject prepare
and publication. The geometry guide retains the global explicit-user-request
rule for technical drawing work.

## Provider catalogue compatibility

The provider's128-function limit is unchanged. The host reserves a slot for an
enabled HTTP `result_get` helper before composing the shared Team catalogue.
Only when space is needed, exact proposal reading is advertised through
`team_context{proposal_id}` instead of its redundant `team_proposal` name. If
another slot is necessary, release is advertised through
`team_reserve_write{release:true,reservation_id}` instead of `team_release_write`.
Schemas, role instructions and proposal notices identify the active aliases.

The original proposal read and release paths still perform the operation,
including owner/token checks and idempotent release. Neither alias applies,
rebases, renews or grants a write. Legacy dispatch and non-crowded catalogues
remain available; CAD capabilities are not dropped to fit. A catalogue that
still cannot fit rejects explicitly. Isolated part specialists retain their
private interface and cannot use aliases to browse the parent Team store.

## Obsidian

Each new code export has an attested `ARK Context.md` start page and a location-
coverage receipt. Both carry the current source/graph identities and real links
to exported notes/canvas. Unlocated symbols, unsupported syntax and partial
extraction remain explicit limitations. A graph link grants no write authority.

Combining memory now checks that the selected Obsidian snapshot and current code
index describe the same source/graph, and checks that inventory again before
publishing. A stale/mutated input leaves the earlier current pointer intact.
Refresh code export first; memory anchors still carry their own current/stale
source status and historical attribution. No personal vault is overwritten.

## Verification

- PowerShell code-context tests: **55 checks passed**, actual pinned offline
  Graphify on synthetic source; zero model calls.
- Combined memory-vault tests passed, including real held-reader publication
  contention, source tampering, stale code-index rejection, prior-pointer and
  edited-export preservation, and current/stale/unsafe anchor handling.
- Native test coverage added for pagination, large boundaries, cursor staleness,
  incoming/outgoing205-leaf stars, dense256-edge slices, aggregate edge identity
  coverage, deltas, no evaluation/mutation, geometry specialist discovery and600KB synthetic
  native payload exclusion from graph overview. Native integration passed:
  783 graph checks, 233 onboarding checks, 349 geometry-node checks and the
  complete 210-suite native/script regression. Team catalogue, candidate custody
  and read-only review checks passed. No live-provider quality comparison is claimed.
- Compatibility regressions exercise normal/isolated Team catalogues with and
  without result handles, exact proposal read parity, actual foreign-owner
  release rejection, private-scope denial, and real node prepare/owned apply
  versus foreign/readonly attempts. Integration execution remains the delivery
  gate for these follow-up fixes.
