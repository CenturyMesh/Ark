# Portable saved geometry identity — 032D

Milestones A.01, A.02 and D.02: native saved geometry now records a versioned
`native_cache_provenance` receipt containing the producing worker/runtime digest
and a SHA-256 digest of the resolved native request. The physical `.ark` container
and authored document formats do not change.

The previous portable entries used the local cache key alone. That key includes
the worker and DLL bytes and the host scheduling thread count. An application
update or a different thread budget could therefore prevent matching any saved
entry, causing full recipe reconstruction before the restore validator was even
called. Existing same-producer, same-request entries remain compatible.

New request receipts preserve every geometry and display input. Only the
host-added, top-level `native_threads` scheduling field is excluded. A producer
change does not permit direct cache reuse: a matching saved request must still
pass `restore_native_result`, which reads and validates the B-rep, remeasures
current exact properties, regenerates display geometry and validates references.
Operation-specific historical evidence remains quarantined as saved evidence;
unsupported operations reconstruct normally. The result records the source
producer separately from the current producer of the fresh validation receipt.

Local cache hits still require the original producer-bound exact key. The new
request index exists only inside the owning loaded document's staged results and
is active only during that document's evaluation. Unknown/malformed provenance
schemas are discarded explicitly. Ambiguous portable request bindings are not
used for cross-producer lookup. Corrupt geometry rejects validation and falls
back to normal reconstruction. Changed geometric inputs do not match.

An old entry contains neither the original request digest nor its producer
identity. These cannot be recovered from its opaque hash. When its old exact key
does not match, one reconstruction and save is necessary to create the new
receipt. The implementation does not infer missing provenance or promise that
existing older files open without that first reconstruction.

Synthetic regression coverage includes changed scheduling and producer identity,
changed dimensions, corrupted B-rep with a valid transport digest, malformed
receipt schema, and legacy exact-key/fallback behavior. Runtime results and
opening measurements belong in the iteration verification evidence; no universal
opening-time guarantee follows from this transport change.

Multi-input booleans use a distinct `model_boolean_sequence` host envelope. Its
schema contains the operation and all ordered original input B-reps, topology,
semantic namespaces and occurrence frames. A cache miss runs the existing
pairwise boolean implementation unchanged. The final feature result is keyed by
the complete sequence, so portable restore validates that final B-rep without
reconstructing intermediate prefixes which were never standalone feature bodies.
The envelope is not dispatched as an ordinary pairwise worker request. Old files
without aggregate receipts need one reconstruction/save for this optimization.
