# mapbox/earcut.hpp

Unmodified upstream header and ISC license, pinned to release **v2.2.4**
(release commit `4811a2b`). Retrieved 2026-09-22 from:

- https://raw.githubusercontent.com/mapbox/earcut.hpp/v2.2.4/include/mapbox/earcut.hpp
- https://raw.githubusercontent.com/mapbox/earcut.hpp/v2.2.4/LICENSE

SHA-256:

| File | SHA-256 |
| --- | --- |
| `earcut.hpp` | `3b838bc3f643d34ced850b6d53b19e2d9abdf6c2c067425b02868d131379b3cc` |
| `LICENSE` | `828f2aed51b6526881a236758ec9b08cd69928fbfc70346d9d44a0b3a3444fe1` |

ARK uses this header only to triangulate caps of validated simple sampled
extrusion profiles containing disjoint interior through-holes. ARK validates
loop intersections/containment, cap area, indexed mesh topology and volume.
It rejects unsupported or failed geometry rather than repairing vertices.
This is sampled polygon geometry; it is not exact NURBS/B-rep certification.

Hole-bridge triangulation may omit a collinear boundary point or leave a cap
edge passing through an existing boundary point. ARK subdivides that cap edge
at the unchanged original point so cap and wall indices agree. Numerically
zero-area bridge triangles are excluded using an explicit f64 arithmetic
roundoff bound; all supplied boundary vertices are retained. Final cap area,
closed oriented edge topology, and independently calculated extrusion volume
must still pass. No boundary point is welded, moved, or replaced.
