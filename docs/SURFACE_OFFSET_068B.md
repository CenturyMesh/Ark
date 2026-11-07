# Checked surface offsets and thickness — 068B

**A.02 / B.03 / D.02 — Verificado no escopo local.** The following contract separates exact
candidate checking from sampled curvature screening and manufacturing approval.

`surface_control` adds two actions, both requiring the observed
`expected_revision`, one feature `id`, `mode:offset|thicken`, a signed `amount`
in millimetres or a named parameter expression, and optional `samples` from 3
to 33 per UV direction.

- `offset_check` privately evaluates the native construction and reports current
  B-rep topology/interference checks, per-face curvature samples and located
  candidate faults when the native checker provides them.
- `offset_plan` additionally requires the inspected `source_brep_sha256`. It
  rejects failed trials, creates an ordinary retained `doc_preview` candidate,
  and leaves publication to explicit `doc_apply` at the same revision.

The diagnostic accepts face/shell sources with 1–64 faces. UV points outside
trimmed regions are excluded; undefined curvature stays explicitly counted.
`sampled_min_radius_mm` is null for planar or unavailable samples. Hotspots are
the highest sampled ratios between absolute requested length and curvature
radius. Their locations come from native surface derivatives, but those ratios
are independent of offset sign and are **risk indicators, not a proven failure
cause or a continuous bound**. Native fault bounds describe actual rejected
candidate subshapes; a builder failure without such shapes remains unlocalized.

The ordinary offset and thickness producers also check their resulting shape
with `BRepAlgoAPI_Check` (topology/self-interference, without using its small-edge
filter to discard geometry). Restored offset/thickness geometry is checked again.
The producer reports `offset_intersection_checked` or
`self_interference_checked`; this is a named native check, not a proof that every
possible surface defect has been excluded. No thickness, tolerance, source
surface or solid is silently changed to make an operation pass.

Studio exposes **Check surface offset / thickness**, native risk markers,
**Offset diagnostic details**, and **Preview checked offset / thickness**.
The preview shows the actual candidate image and offers Apply/Cancel. Applying
creates one derived definition; existing assembly placements stay unchanged.
Markers follow the inspected occurrence frame and disappear when it is hidden,
replaced or moved. Publication still checks document revision/epoch and source
geometry. Undo restores the prior document.

Verification source: `surface_offset_068b_tests.cpp` independently compares a
20×20×2 thickness with 800 mm³, checks planar sample accounting, and compares the
centre curvature radius of an authored parabolic cylinder with 10 mm. It also
checks no-mutation diagnostics, retained preview, explicit apply, Undo, stale
revision/hash rejection and invalid inputs. The Studio selftest uses actual
forms, candidate image, Cancel/Apply and stale rejection. Executed results and
captures are recorded in the iteration evidence.
