# Design quality investigation

**A.03/D.02 — Em curso.** Source-only follow-up to 075D; no new distributed
iteration or measured design-quality improvement is claimed.

The current provider policy and static onboarding share one design-brief rule.
An editable concept is a starting checkpoint. The agent must develop it to the
requested detail and function, account for requested components and defining
shape details, and inspect the authored result before finishing. A rejected
operation calls for diagnosis and an equivalent valid construction, or an
explicit unresolved limitation. It does not authorize silent loss of intent.
Engineering justification must connect calculations to authored geometry and
distinguish measured inputs, sourced evidence and assumptions. CAD/BOM success
alone does not establish performance.

This is guidance, not a new automatic completion gate or geometry validator.
Read-only role permissions, concept-first authoring, conditional manufacturing
checks, atomic publication and revision guards remain in force. There is no new
turn/token budget. HTTP, Claude Code and ACP build their provider instructions
through the same request builder; static guides use the same policy text.

## Acceptance evidence still required

### Historical public example replay

`examples/airplane.arkops` entered Git at `f6699e5` on 4 October 2026.
Its 62 commands author six parameters, 24 planes, 25 sketches and seven
features: six lofts and one revolved surface. The fuselage uses seven sections;
the wing uses five. This establishes an earlier authored geometric example,
not the original model, user brief or provenance of its agent interaction.

An isolated session of the installed 075D MCP executable accepted the original
command file unchanged. All seven features evaluated without failure and all
seven native property reports returned `brep_valid:true`. Six outputs are solids;
the spinner is the explicitly authored surface. The first observed recompute
took about 1.37 seconds; this single run is not a performance comparison.
The current software snapshot was inspected. This confirms that the current
runtime can still construct this example, not identical topology to an old
binary, autonomous agent performance, printability or flight performance.
Receipts and executable/source hashes: `out/verification-076d/example-replay`.
No live Studio document was loaded or changed by these isolated replays.

The literal main provider instructions at `f6699e5` contain 891 characters
(133 whitespace-separated words); the released source at `4870937` contains
8,952 characters (1,211 words), excluding tool schemas, onboarding and history.
The source follow-up's shared guidance change is excluded from this count.
This is a measurable input change to investigate, not a causal attribution.
There has been no matched live model comparison or instruction ablation.

Compare historical and current behavior with identical briefs, reference inputs,
model/version, effort, connector and initial documents. Retain every attempt,
including rejected operations and abandoned constructions. Use isolated
synthetic cases; do not publish private drawings or conversations.

Review outcomes against the original brief: component and detail coverage,
proportions and interfaces, valid editable geometry, preservation of intent
after failures, and evidence supporting requested functional claims. Do not
score designs by primitive count, surface count or visual complexity alone.
For image reconstruction, compare visible details as well as silhouette and
proportions; hidden geometry remains an assumption. Keep independent human
review separate from machine-checkable properties.

The existing [agent benchmark](AGENT_BENCHMARK.md) supports retained attempts
and qualified acceptance. Offline scripted transports test connector behavior;
they cannot establish model reasoning, reconstruction fidelity or restoration
of historical design quality. The causal regression and matched live quality
comparison remain unresolved.

## Local verification

Six existing suites passed: `onboarding-current-075d`, `assistant-connector`,
`assistant-acp`, `assistant-claude-code`, `agent-workflow-033d` and
`specialized-component-patterns`. This covers build, guide refresh, provider
contracts and existing authoring flows, not live design quality. Receipt:
`out/release-068B/dev-build-20261108T232332438Z.json`.

The same six suites were rebuilt and passed again before source publication on
10 November 2026. Receipt:
`out/release-068B/dev-build-20261110T190649291Z.json`.

The first attempt passed five suites and failed an exact-text assertion for
preserving user constraints. The shared policy now retains that existing
sentence verbatim; no test expectation was weakened. The failed receipt is
`out/release-068B/dev-build-20261108T232207003Z.json`.

Only test targets were built for this follow-up. The installed Studio, release
identity and user documents were not changed; no new installer was produced.
Publishing this source for review does not qualify or distribute a new runtime.
