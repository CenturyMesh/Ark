# Agent outcomes, 051A

D.02 and A.02: **Verificado no escopo** for the offline harness checks below;
real provider comparisons remain **Parcial**. No paid provider was called.

The existing `ark-benchmark` harness now records distinct `completed`,
`postconditions_passed`, `persisted` and `accepted` outcomes. Acceptance requires
completion, no terminal error/timeout, the native oracle, and successful persistence.
Every attempt, including a failed one, remains in the report and strategy denominator.

`benchmarks/agent_outcomes_v2.json` adds three synthetic cases: a driving parameter
edit with an expression-preservation check, an assembly placement edit with exact
clearance, and recovery after a rejected two-command edit. Before the recovery run,
the host executes the invalid batch and records the actual rejection, before/after
revision and authoring SHA-256. Recovery counts only if that rejection was atomic.
The model receives the brief, acceptance criteria and rejection receipt. The reference
repair is available only to preflight and the deterministic offline transport.

Each attempt reserves a unique directory, saves `result.ark`, hashes its actual bytes,
loads it, reconstructs authoring without the saved derived geometry in a fresh session,
and reruns the same independent oracle. It compares authoring and checks file bytes
again after reopen. `persistence.saved` means writing succeeded; `persisted` also
requires the cold oracle and authoring comparison. A completed response or an in-memory
valid body alone cannot satisfy this gate. `artifact_directory` in the config selects
the parent; otherwise files remain under the system temporary directory's
`ark-agent-benchmark` folder. These artifacts contain synthetic fixtures only.

Every attempt includes the case version and SHA-256 of its normalized definition
(including oracle and reference), application identity and running executable SHA-256.
The executable fingerprint does not attest loaded native DLLs: comparisons must also
record the selected SDK/runtime evidence. Fixture hashes are sensitive to JSON field
order; use the same versioned fixture file for comparable runs.

Run the new suite through the existing CLI:

```powershell
ark-benchmark --suite benchmarks/agent_outcomes_v2.json --preflight
ark-benchmark --suite benchmarks/agent_outcomes_v2.json --repetitions 1 --out out/outcomes.jsonl
```

An optional config `tariff` object accepts `provider`, `model`, `currency` (three
uppercase letters), `as_of` (calendar date), `source`, `basis` equal to
`flat_input_output_tokens`, and nonnegative finite `input_per_million` and
`output_per_million`. These are explicit user-supplied estimates, not verified
provider charges. The flat input rate includes cached tokens; output includes
reasoning tokens. Discounted-cache, tiered and subscription billing are not modeled.
No rate is invented or downloaded. Rates must match the configured provider/model.
Offline runs, absent tariffs, missing/partial usage or a mismatch between accounted
responses and provider requests produce `cost: null`. Strategy summaries sum all
known attempt costs, including failures; cost per accepted outcome stays null if
any attempt cost is unknown or none is accepted. Provenance is retained with receipts.

Validation: the extended `agent-benchmark` test passed locally with actual native
executor calls and real .ark files: the original 30 preflights and nine strategy
runs, plus three v2 preflights and nine v2 strategy runs. Negative checks cover
completion without acceptable geometry, save failure, changed authoring on reopen,
atomic rejection/recovery, incomplete usage, mismatched tariff, and failed-attempt
cost accounting. The changed benchmark and test translation units were compiled
with LLVM-MinGW and linked against the existing ARK baseline library for this local
check; this is not a new packaged runtime or a live-model speed/cost comparison.
