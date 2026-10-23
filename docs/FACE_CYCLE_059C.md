# 059C — exact face traversal in the viewport

B.01 / A.04 / A.02: implemented for local native verification. This note does not
claim a distributed release or manufacturing qualification.

Click a body in Body selection mode, then press Tab to select its first eligible
exact face. Further Tab presses cycle forward; Shift+Tab cycles backward and,
before the first forward step, starts at the last face. The cycle wraps. The
selected body occurrence and Body mode remain active, so clicking another body
starts a new cycle. The status line identifies the selected face and its position.

Traversal uses the producer's canonical face order and preferred bound semantic
reference, including current-B-rep generated references where no named reference
exists. Repeated mesh spans and aliases do not create extra faces. The selected
reference retains the feature, assembly node and pattern copy; the cycle retains
the producer's B-rep SHA-256 and immutable native payload identity. Display mesh
triangles only locate and highlight these references, never define CAD topology.

Revision, document epoch, display generation, selection generation, occurrence
identity, native payload and placement guards prevent silent rebinding. Hidden
occurrences, absent exact references, sampled meshes, flat/stock/deformed previews,
mechanical or baked playback, inertial playback and exploded display are rejected
explicitly. Suspension and segmented source/notched-tube previews are outside this
first traversal scope. Re-click the body in its authored static / Formed view to
restart after a rejection. Existing sketch, dimension and direct-edit Tab shortcuts
take precedence; native text controls retain keyboard focus. Explorer-only
selection does not arm a cycle until the body is clicked in the viewport.

No geometry, placement, document selection transaction or revision is written.
The canonical list is built on the first Tab after a body click, not every frame.
Subsequent steps use the cached list and existing selection render buffers; they
do not recompute B-rep geometry, serialize the document, or invoke a native worker.

## Verification contract

`studio-selection-picking-ui` now sends native mouse and keyboard messages through
the production window procedure and verifies:

- six unique exact box faces, deterministic order, wrapping and reverse traversal;
- a third patterned copy and a separately placed occurrence of the same definition;
- the selected face fill and unchanged authored document/revision;
- native Edit focus, including a forwarded Tab message;
- rejection after a real exact source edit, hiding, sampled-mesh selection and an
  explicit unsupported stock-display state;
- captures `.tab-pattern-face.png` and `.tab-exact-face-final.png`.

The stock-display guard probe sets the display flag in the isolated fixture; it is
not evidence of a complete stock authoring workflow. Native compilation, test
execution and screenshot inspection are pending the integrated 059C build.
