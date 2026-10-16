# 032D — compact native payloads and shared display geometry

**A.04/D.02/D.04 — Verificado no escopo.** `NativePayload` and `MeshSnapshot` give body
copies value semantics over immutable, shared storage. This is ARK's own storage
implementation; no external CAD implementation was copied, and no superiority
claim follows from the class names.

`NativePayload` separates producer metadata from the large display mesh numeric
arrays. Positions retain their original `float64` values and indices use exact
`uint32` values. B-rep bytes, topology, reports, face spans and edge metadata keep
their values. No geometry is simplified or quantized. Unsupported or malformed
array representations stay unpacked and retain the normal validation path.
Duplicate object fields are not silently normalized.

Ordinary report/reference reads avoid expanding mesh numbers back into JSON
objects. Display conversion consumes the typed arrays directly and produces the
same existing float32 mesh and normals. Full JSON reads use one thread-safe lazy
expansion shared by its readers; that expansion preserves numeric values and
field order. Picking and colored snapshots read only the needed mesh metadata.
Saving derived geometry avoids creating numeric JSON arrays that it would then
discard.

`MeshSnapshot` owns a `shared_ptr<const Mesh>`. Copying a body shares this owner
and the immutable native payload. Native data or mesh edits construct a new
value; they cannot mutate an existing cached body, prepared candidate or other
occurrence. Portable geometry builders construct mutable meshes locally before
publishing the immutable result. The viewport still uses its existing cached
definitions and occurrence placements.

Retained-preview accounting deduplicates native and mesh owners across bodies
and excludes storage still owned by the live document. It conservatively charges
the optional full JSON expansion before allowing a new candidate, preserving the
128 MiB retained limit. This is not a process-wide memory ceiling: native worker
transport, temporary editing buffers and explicitly requested full JSON have
separate allocations. Packed numeric arrays and immutable meshes remain runtime
derived data; this change embeds neither STEP nor display meshes in `.ark`.

The public synthetic storage fixture compares estimated owned allocation
capacities for the same mesh data. It verifies numeric/JSON roundtrip parity,
identical display buffers, shared owner identities, independent edits, bounded
expansion, concurrent reads and unchanged malformed-input rejection. Measurements
and integration results are recorded in [032D](iterations/032D.md). This fixture
does not compare provider tokens, another CAD product or manufacturing quality.
