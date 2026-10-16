# Patterned rigid followers — 032D

B.04/D.04 remain **Parcial**. This change extends existing assembly drive,
constrained continuation and sampled inspection to rigid pattern followers.
Its local verification is recorded below; it does not establish dynamic
physics, continuous collision clearance or manufacturing qualification.

Selecting an ordinary joint driver carries every occurrence on its moving
side, including linear and solved sketch-driven hardware patterns. Stable
copy numbers, skipped slots, feature definitions, visibility and parent
relationships are retained. A passive downstream joint keeps its existing
coordinate during an ordinary bridge drive. Constrained mechanisms continue
to solve their actual geometric and mechanical equations. Each pattern copy
receives its own seed's solved rigid displacement, retaining its relative pose.
Grounding propagates through full-frame rigid attachments.

Existing tool calls remain available:

```json
{"drive":{"driver":"Shoulder","mate":1,"mode":"rotate","delta":5}}
```

This `assembly_info` query is read-only. An `assembly.drive` command with the
same selection commits through `doc_preview`/`doc_apply` and the current
`expected_revision`. Sweep deltas remain relative to the current solved pose.
Existing joint limits and residual tolerances apply to every requested pose.
A failed limit, source or constraint check rejects the complete transaction or
sweep, preserving the previous document and history.

## Retained whole-pattern pose

An active component pattern can retain optional `carry_frame`, an array of
16 column-major numeric matrix entries. It is a finite, bounded, proper rigid
transform in the parent coordinate system. Its translation entries are in mm.
Scale, shear, reflection, nonaffine matrices, nonfinite entries and coordinates
beyond the existing 1e6 placement bound reject. Proper rigid validation uses
the existing pattern frame tolerances.

For parent frame `P`, carry frame `F`, original expression-based node transform
`L` and a pattern slot transform `D`, the authored seed is `P * F * L`.
The whole layout is carried as `P * F * D * L`, followed by any seed displacement
from the exact existing mate solve. The identity default preserves the previous
pattern placement semantics. Driving a patterned follower updates only `F`
through an explicit complete pattern edit recipe. It retains the source,
count, spacing, skipped slots and original `node.p` expressions. Subsequent
parameter or independent sketch/feature source edits still evaluate normally.

Nonidentity carry frames conditionally require document source version **44**.
Identity is omitted when saving, and documents with ordinary identity patterns
keep their previous required source version. Readers reject an explicit
`carry_frame` field in older source formats. Saved history retains the carry
recipe, including across reopen, undo and redo. The application's release
identity is independent of this conditional document requirement.

## Supported boundaries and diagnostics

Linear and sketch-driven rigid followers are covered by the dedicated motion
fixtures. The same whole-pattern representation carries circular and
independent guide-driven layouts; their existing guide/source validation and
alternate-definition routing remain in force. General pattern feature
qualification remains bounded by the existing pattern producer contracts.

A selected patterned **driver** remains ambiguous and rejects with
`MANIPULATION_PATTERN_DRIVER_UNSUPPORTED`, including the node ID. Select the
ordinary joint carrier instead. Explicit nonexistent mates, unsupported joint
types and incompatible drive modes have separate diagnostics naming the mate.
An actual absence of an eligible grounded moving side retains
`MANIPULATION_NO_GROUNDED_JOINT` and names the driver and explicit mate.

An occurrence-linked `pattern_driven` layout or `chain` alternate-node seed
can change when its source occurrence moves. If that change cannot coexist
with the requested rigid layout using one carry frame, the motion rejects
with `MANIPULATION_PATTERN_SOURCE_CONFLICT`, naming target, source and mate.
The source dependency is retained. No copy is discarded, frozen or silently
rebound. Ordinary independent sketch/feature sources do not have this
occurrence-source ambiguity.

Exact sampled collision inspection still uses every selected stable copy's
actual world transform. Bound cable endpoints and obstacles use those same
placements and retain their existing producer dependencies and native checks.
Sampled clearance proves only the requested samples; no between-sample
clearance or dynamic load evidence is inferred.

## Verification record

`tests/assembly_motion_032d_tests.cpp` uses public synthetic fixtures for:

- Nested, tilted parent groups and rotated seed placements; a two-joint chain
  with skipped linear/sketch patterns and unchanged grounded hardware.
- Per-copy preview/commit equality, original expression/source retention,
  associative spacing/source edits, and explicit failed selections.
- Prepared candidate publication, atomic stop rejection, malformed carry
  matrices, conditional source44, binary/JSON reopen, persisted undo and redo.
- Actual geared continuation with hardware on both independently moving
  carriers, retaining their distinct copy offsets and feature identities.
- Native-enabled sampled exact clearance of non-seed slots and actual cable
  recomputation using a bound patterned occurrence, including post-commit
  cache invalidation and a later stop failure.

Syntax-only C++17 checks passed for the changed assembly/pattern sources and
the portable/native-enabled test source. The portable runtime passed **2,465
checks** and native-enabled runtime passed **2,567 checks**, including exact
sampled copy clearance and cable recomputation. Initial integration attempts
exposed two test-fixture errors: JSON
object key order was compared as bytes, and a native collision report was
queried for a mesh-only field. Both assertions now use their actual contracts.
No private user design is a test fixture or distributed artifact.
