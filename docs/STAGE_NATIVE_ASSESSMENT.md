# Native sheet geometry assessment for ARK

Historical engineering assessment dated 2026-09-23. This public version retains
ARK design criteria and test ideas; internal source-checkout inventories are
omitted. Its proposed implementation scope reflects that date. Current native
support and limitations are documented in [sheet metal](SHEET_METAL.md),
[source STEP](SOURCE_STEP.md) and [validation](VALIDATION.md).

## Geometry and publication criteria

1. **Import and identity.** Inventory exact solids and units from original STEP;
   require explicit body selection when multiple solids exist. Preserve the
   source-byte hash and physical occurrence binding. Face/body ordinals are
   trace coordinates rather than geometric authority.
2. **Source-owned recognition.** Prove thickness from opposed material skins;
   identify panels and physical bends from paired faces, complete contacts,
   cut walls and topology. Missing or competing ownership must reject.
3. **Declared allowance.** Separate source geometry from the forming rule.
   For a simple cylindrical bend, caller-declared K gives neutral radius
   `r_n = r_i + K*t` and allowance `BA = abs(theta)*r_n`, within proved skin
   bounds. Other allowance rules need separate typed inputs. The tube midpoint
   convention in [source STEP](SOURCE_STEP.md) is distinct from a sheet K rule.
4. **Atomic publication.** Develop a complete source panel/bend/feature graph;
   validate material ownership, coverage, topology and contours independently.
   Publish accepted exact geometry or a typed rejection. Reference fitting,
   fixture-selected geometry and hidden tolerance relaxation cannot repair
   an unsupported production case.
5. **Consumer boundary.** Inspecting all source bodies is separate from
   flattening an explicit body. Viewers and external consumers retain the
   accepted publication's identity. Display meshes cannot replace its contours.

## Proposed initial native domain

The initial proposal was a bounded C++/OCCT path: one uniform-thickness solid,
two planar flange regions and one constant-radius cylindrical bend with paired
skins, complete tangent contacts and no cuts through deforming material.
A flat panel was the baseline. Complex or ambiguous cases required named
rejections until independently supported. This is historical planning;
consult [sheet metal](SHEET_METAL.md) for the implemented domain.

Keep source inventory, thickness/skin proof, bend/contact proof, allowance,
developed material and independent validation explicit. Native B-reps and
curves remain authoritative through export; tessellation serves the viewport.

## Verification ideas

- Positive: flat panel; simple bend with declared K and preserved panel holes;
  valid STEP readback and measured source/refold agreement.
- Hostile: ambiguous body, competing skin/face roles, incomplete contact,
  bend-crossing holes, duplicate occurrence, changed source bytes, invalid
  allowance, missing ownership, overlapping material or stale publication.
- Metamorphic: rigid transform, mirror, positive scale under a declared
  tolerance policy, face/body reorder and supported topology splits preserve
  intrinsic classification and transformed exact results.

Native implementation and tests belong in C++. Runtime bounds and cancellation
need ARK-specific evidence. External fixtures and reference DXFs can compare
an immutable accepted result; they cannot choose production geometry.
