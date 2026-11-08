# Face anchors and explicit body selection — 074B

**B.01/B.02/B.03/D.02 — Verificado no escopo local.** The native G2/G7 contracts
have focused local coverage. The rebuild-evidence panel and body-authoring
controls passed normal/compact Studio tests. Broader milestone qualification,
complete release gates and distribution remain separate.

The **Exact solids** tool group includes **Anchor a face reference** and
**Select solid bodies**. Both start from a current exact solid definition and
create a named, editable feature with that source as its explicit input. Open
surface sheets are outside these NativeSolid operations.

For a face anchor, choose a published source face or all current faces and enter
an alias plus a point in source-definition coordinates. Coordinate fields retain
expressions. Native validation requires the point to lie strictly inside exactly
one current face, or one verified descendant of the selected split face. It does
not choose the nearest face. Boundaries, ambiguity and unavailable lineage are
rejected. Curved faces can be anchored; this alone does not create a missing
planar frame descriptor. The result preserves the complete source geometry and
adds the semantic alias.

For body selection, choose either checkboxes from the producer's current body
inventory or one interior point per retained solid. Listed body identities are
bound to the exact source B-rep digest; a subsequent incompatible source change
rejects stale IDs. Interior points retain coordinate expressions and are checked
again against the current source. Points on boundaries, outside every solid,
inside overlapping solids or repeated within the same solid reject explicitly.
If a source does not publish a current body inventory, only point selection is
offered; Studio does not invent body IDs.

**Preview** evaluates a retained exact candidate and displays its actual geometry.
The review reports the result's solid count and volume; body selection also
reports retained and explicitly omitted volume. **Apply** publishes that same
candidate once; **Cancel** leaves source/revision unchanged. The original source
feature remains editable even when some bodies are omitted from the new result.
One Undo reverses the accepted feature.

Revision, document epoch and the current immutable source owner are checked
between source selection, native reads, preparation, image capture and Apply.
Candidate evidence is read synchronously and its custody is restored before any
progress/UI message loop. Source report/topology reads use metadata without
expanding the stored display-mesh JSON. No reference is silently rebound.

Source semantics are version 59 when these operations are used. This is
independent of the physical document container version.

The new UI harness is `--self-test-body-authoring-ui`. It uses synthetic split
solids and checks native face aliases, expressions, exact body IDs, point-based
selection, preview/Cancel/Apply/Undo, persistence, compact controls, stale-epoch
rejection and boundary failure. The companion `--self-test-rebuild-diagnostics-ui`
checks the read-only **Why was it rebuilt?** panel, including feature IDs/reasons,
normal/compact layout and separate preparation/publication evidence. Final
results and screenshots belong in the iteration verification record.

The first central Studio checkpoint (`20261108T163336299Z`) passed both rebuild
panel runs, 52 checks each. Normal/compact report captures and the first anchor
form/exact-candidate captures were inspected. The initial body UI run reached
valid anchor creation, source-expression regeneration and two Undo operations,
then its full-document string oracle failed: Undo intentionally keeps ID
allocation counters monotonic. The corrected test compares the complete
canonical authored document after normalizing only those checked monotonic
counters and uses actual newly assigned IDs for later results. This was a test
expectation correction; production Undo and native tolerances were unchanged.

The corrected central checkpoint `dev-tests-20261108T164649319Z.xml` passed all
eight selected UI runs. Body authoring passed **110 checks per run** and rebuild
diagnostics **52 per run**, for 324 checks across these four runs. Captures of the
normal anchor controls/candidate, compact body checkboxes/candidate, compact
point-expression controls/candidate and normal/compact rebuild reports were
visually inspected. The compact dialogs fit the 960 × 640 work-area override;
the real candidate image, scrollable evidence and Apply/Cancel controls remain
available.

The body fixture retains two 640 mm3 source solids. The face alias keeps their
1280 mm3 geometry; changing the driving height from 4 to 6 updates the anchored
point and total volume to 1920 mm3. Selecting either published ID or an interior
witness retains one 640 mm3 solid. Widening the unselected region keeps the
point-selected result at 640 mm3 and reports 960 mm3 explicitly omitted from the
result. Stale-epoch Apply and a boundary witness reject without publication.

Local evidence is under `out/release-068B/ui-checks/`, with prefixes
`body-authoring-normal-074b.png`, `body-authoring-compact-074b.png`,
`rebuild-diagnostics-normal-074b.png` and
`rebuild-diagnostics-compact-074b.png`. These are synthetic fixtures and native
Studio controls, with no live user drawing/session used as test data.
