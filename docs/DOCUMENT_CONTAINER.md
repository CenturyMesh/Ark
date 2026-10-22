# Native ARK document container

Saving a `.ark` file now writes ARK's binary container. `.arkjson` and `.json`
remain explicit JSON exports. Loading detects the physical format from its
signature, so existing JSON `.ark` files remain readable. An explicit save of a
legacy `.ark` publishes a binary replacement through a staged sibling file.
Opening a document alone does not migrate or modify it.

The binary container is an owned SQLite schema using the already vendored
SQLite library. `application_id` is `0x41524b31` (`ARK1`) and `user_version` is 1.
This physical version is independent of the authored document version, including
the version-27 iteration journal. The existing C++ document parser, authoring
rules, journal checks and exact geometry authority remain the semantic contract.
Container version 2 (`user_version` 2) adds an optional `ark_derived` table of
saved native geometry, with a 256 MiB decoded and 128 MiB compressed capacity
and a 256 MiB physical file limit; see [formats](FORMATS.md). Since 049A it also
carries cached baked-motion frames, verified against their receipts on open
([derived bakes](DERIVED_BAKES_049A.md)). Since 051A the whole section is one
Zstandard pack split across `pack1.<manifest>.<index>` rows; per-entry rows from
earlier runtimes still load, and the table schema and container version are
unchanged. The 64 MiB physical cap below applies to version 1; the authoring
limits apply to both versions.

## Storage and incremental writes

The schema contains three tables:

| Table | Contents |
| --- | --- |
| `ark_meta` | Logical JSON byte count, full canonical typed-root SHA-256, monotonically increasing save generation |
| `ark_payload` | Immutable content-addressed typed chunks and raw large-string blobs |
| `ark_slots` | Current manifest and named document slots referencing immutable chunks |

The typed value codec begins with `ARKB` plus codec version 1. It encodes null,
boolean, exact small integers, finite binary64 numbers, strings, arrays and
objects. Lengths use bounded canonical varints. Object keys are sorted and
duplicate keys are rejected. Numbers retain their binary64 value, including
negative zero; strings retain their bytes. Every payload hash covers its kind
byte and its payload bytes, preventing raw-string/typed-chunk namespace overlap.
This format contains binary values rather than a monolithic JSON text blob.

Top-level arrays store one chunk per authored entry. Iteration history stores
its metadata, immutable nodes, baseline and shared blob dictionary separately.
Strings of at least 1024 bytes use content-addressed raw blobs even when they
occur in more than one current or historical chunk. The in-memory journal's
existing semantic blob IDs and replay rules remain unchanged.

A subsequent save starts one `BEGIN IMMEDIATE` transaction, inserts only new
payloads, updates only changed slot references and removes unreachable payloads.
Unchanged source blobs and history nodes retain their existing rows. A metadata
generation still advances on an unchanged save. SQLite updates the affected
pages using `synchronous=FULL` and a DELETE rollback journal. After a successful
close, the document is one file and has no required WAL/SHM sidecars. A write
conflict fails the save; it does not silently merge another process's document.

An initial save and a legacy JSON migration use a unique sibling stage and an
atomic replacement after SQLite closes. A failed transaction rolls back. A
process interrupted after dirty pages reach disk leaves a rollback journal;
the next load permits SQLite's recovery before reading the validated snapshot.
This recovery requires write access to the file/directory when a hot journal
exists. An ordinary clean container remains readable with read-only access.

SQLite keeps the pages freed by a save for later reuse; the file does not
shrink by itself. Since 051A, when a committed incremental save leaves at least
a quarter of the file and at least 1 MiB free, the same connection writes a
compact copy with `VACUUM INTO` to a unique sibling stage. That copy is loaded
with the normal checked reader and must reproduce the committed authoring hash
and derived entry count; only then does an atomic replacement publish it. A
failed verification or replacement (for example, another process holding the
file open on Windows) removes the stage and keeps the committed incremental
save. The window between closing the connection and the replacement is not
locked; ARK does not support concurrent writers to one document.

## Limits and rejection rules

The existing **4 MiB logical document/journal cap is unchanged**. The physical
container has a separate **64 MiB cap**, 4096-byte pages and a bounded page count.
Typed values and the total stored payload bytes have a 16 MiB ceiling. Loading
also bounds slot/payload row counts, nesting, value items, and cumulative string
expansion before materializing referenced data. Reconstructed canonical JSON
must match the stored logical byte count and satisfy the 4 MiB cap.

Reads and incremental writes check the ARK application ID, physical version,
exact schema, page size, journal mode and page/foreign-key integrity. Reads check
all payload SHA-256 values, the full canonical typed root checksum, complete
manifest references and absence of unreferenced data. Truncated or corrupt
containers, unknown versions, extra schema objects and foreign SQLite files
are rejected. An existing foreign SQLite file is preserved on a save attempt.
No arbitrary SQL, schema extensions or dynamically loaded SQLite extensions are
part of the document API.

Checksums detect corruption; they do not authenticate an author. This owned
format is not encryption, DRM or a guarantee against reverse engineering.
Large source strings can still be read from the blob table. Credentials do not
belong in the document. JSON export remains available for explicit interchange.

## Verification and performance

`ark-document-container-tests` covers typed roundtrips and deterministic
encoding, all codec truncations, unknown tags/versions, nonfinite values and
size/nesting bounds. Container tests cover shared source deduplication,
unchanged incremental saves, a one-parameter chunk update, failed commit
rollback while a reader holds the prior snapshot, and a subprocess interrupted
after writing dirty pages. They also cover checksums, truncation, schema/version
rejection, foreign-file preservation, logical/physical caps, legacy JSON reads,
explicit migration and JSON exports. The existing iteration tests exercise
semantic version-27 persistence and reopening independently of this physical
format.

Run the bounded storage benchmark with:

```powershell
out/live-workspace-release/ark-document-container-tests.exe --benchmark
```

It reports the median of seven saves/loads for a small parameter document and
a roughly 1.5 MiB JSON-shaped fixture containing a shared source string twice.
The latter measures deduplication and a parameter-only update beside an unchanged
large source. Results include initial/incremental save time, JSON save/load time,
container load time, physical size and newly inserted payload bytes/rows.
Fixtures measure codec and file IO; they do not claim evaluated native CAD or
include semantic journal replay validation. The JSON comparison uses the prior
ofstream-flush/atomic-replace behavior; the container uses SQLite FULL durability.

Incremental storage reduces changed bytes on large documents. Every save still
prepares and checks the current authored value tree, and loads verify checksums
and semantic document rules. Faster save or load time across every workload is
not implied; small documents may have higher database overhead. Use measured
workloads before changing the codec, schema or durability contract.
