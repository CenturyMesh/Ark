# Batched Boolean work — 074B

**A.04/B.03 — Verificado no escopo local.** `native.boolean_mode:"all"`
combines the complete explicitly selected argument set through the native
Boolean builder. Parallel mode follows the native thread allowance. The legacy
ordered sequence remains the default for existing recipes; no intermediate
failed solid is accepted or omitted to make a batch pass.

The same final 074B runtime evaluated sixteen overlapping exact boxes, changing
one input width. Both paths produce a valid single solid with analytic volume
7000 mm³ and bounds [0,0,0,70,10,10]. Native disk-result caching was disabled;
each configuration had two warmup edits and five measured edits, with one
feature worker and four CPU threads permitted. Medians include full `doc_apply`:

| Mode | Native thread allowance | Complete apply |
|---|---:|---:|
| Existing ordered sequence | 1 | 269.59 ms |
| All arguments in one native Boolean | 1 | 209.30 ms |
| All arguments in one native Boolean | 4 | 180.10 ms |

This demonstrates a benefit for this input set, not a general guarantee for
small or arbitrary Boolean operations. Thread allowance is a configured ceiling,
not a measurement that every core remained occupied. Different construction
histories may legitimately have different face partitions and lineage; the new
mode is explicit and uses conditional source59.

Full samples, runtime hashes, settings and failures are retained under
`out/performance-074b/boolean-*-final.json`, with the analytic checks summarized
in `storage-boolean-final.json`. Earlier checkpoints remain separately retained.
