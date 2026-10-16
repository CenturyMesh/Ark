# 032D — measured agent response and geometry work

**A.04 — Parcial; D.02/D.04 — Verificado no escopo.** These changes address repeated document-sized
allocations and inventories, scheduling-dependent saved-result lookup and
unnecessary intermediate measurements. They do not establish a universal opening
time, provider token saving or a kernel performance comparison.

## Equivalent public synthetic response measurements

`tests/agent_contracts_032d_tests.cpp` compares the full and compact response for
the same 48-box document and equivalent queries. Bytes below are serialized
`structuredContent` JSON, measured with the same build; they are not model tokens.

| Request | Full bytes | Compact bytes |
|---|---:|---:|
| Current document status | 5,637 | 199 |
| Topic onboarding | 19,279 | 13,632 |
| One-feature preview with a checked postcondition | 40,117 | 2,042 |
| Selected assembly measurement | 7,837 | 1,393 |
| Native box properties | 5,643 | 547 |

The team fixture compares verification request envelopes on the same document:
8,960 bytes with the former broad inspection and 348 bytes with revision status.
Guide responses retain the complete operation instructions; compactness does not
remove failed-check evidence, required source custody or current revisions.

## Geometry and retention

The [native payload and display mesh storage](GEOMETRY_STORAGE_032D.md) shares
immutable owners across body copies and packs display numbers without changing
their values. Its public synthetic allocation check remains separate from process
RSS, private project timings and native B-rep geometric qualification.

Retained previews share immutable evaluated bodies with the live evaluation
cache. The bounded retained allocation budget counts distinct non-live owners,
metadata and receipts across candidates. When the live document changes, budget
accounting charges bodies that no longer have live ownership and evicts bounded
candidates as necessary. Snapshot, transfer and commit materialize mutable views;
these transient copies still have a cost. The [129 MiB public synthetic allocation
regression](../tests/candidate_storage_032d_tests.cpp) tests sharing, cache bounds,
owner identity, payload custody, stale rejection and zero-evaluation cached apply.

[Saved geometry identity](PERSISTENCE_032D.md) separates the resolved geometric
request from scheduling and producer identity. A foreign-producer result always
passes current exact validation. Older unmatched opaque keys cannot be inferred;
they need one reconstruction and save. Native input geometry, topology, parameters
and display controls remain part of the resolved identity.

Holes Orchestrator no longer computes discarded surface areas and exact bounds
after every intermediate cut. It retains the same full B-rep validity, one-solid
census, signed-volume integration and positive-removal checks at each cut. Final
reports still compute area and bounds, using the existing algorithms and
tolerances. No volume-integration shortcut, Boolean tolerance change or solid
discard is introduced.

The evaluator can schedule already-audited explicit-input features beyond
intervening owner-thread rows. Named sketch, datum and occurrence consumers keep
their existing owner-thread restrictions. Results publish in document order;
finished but unpublished jobs retain their worker slot and allocation charge,
preventing an unbounded queue of future geometry. The configured CPU/feature
bounds, cache identities and atomic rejection remain in force. Dedicated native
and portable fixtures check serial bounds/order parity, reuse and rollback. This
scheduling extension has no claimed measured opening-time improvement yet.

## Evidence boundaries

Local portable/native checks, failed attempts and final results are recorded in
[032D](iterations/032D.md). Private design measurements remain private and their
files are excluded from source fixtures, public benchmarks and distribution.
Cold legacy reconstruction, cold validation of newly saved geometry and warm
local-cache loading are distinct workloads. Three-size p95 qualification, hosted
CI, end-user Studio acceptance and manufacturing qualification remain separate.
