# Derived bakes and journal migration — 049A

Milestones **A.02** (work protection) and **C.04** (motion) remain **Parcial**.
This change makes baked-motion frames derived data. It does not change motion
semantics, the solver, clearance mathematics or the 4 MiB journal limit.

## Problem

A real kiosk document reached the 4 MiB logical limit after 36 iterations.
Its current authoring was about 1.2 MB and its journal about 3.76 MB; every
further edit was rejected. Three motions stored their bakes in the authoring,
and almost all of each bake was `frames_f32le_base64`: per-instance 3x4
transforms for every frame. Each re-bake wrote a new copy of those frames into
the journal's blob dictionary, so re-baking was the dominant cost. The frames
are fully determined by the graph, the assembly and the deterministic solver.

## Receipt

`motion.bake` stores a receipt: every 044B bake field except the two float32
payloads, plus two digests.

| Field | Meaning |
| --- | --- |
| `format`, `fps`, `frame_count`, `duration_s`, `instances`, `channels`, `solver`, `clearance_checked`, `dynamics_checked` | unchanged 044B meaning |
| `source_revision`, `source_digest`, `graph_digest` | unchanged; still the authority for staleness (`assembly_changed`, `graph_changed`) |
| `frames_sha256` | SHA-256 of the raw little-endian float32 frame bytes (frame-major, instance-minor, 12 values) |
| `coordinates_sha256` | SHA-256 of the raw little-endian float32 joint coordinates per frame |

A bake is either a receipt or 044B-048B inline frames, never both. A receipt
selects document format 49 ([formats](FORMATS.md)). The 2 MiB per-document
frame bound still applies, counted from the declared frame size.

## Where frames come from

Whenever frames are needed (Studio Baked Motions playback, `motion_info{name,frame}`,
`visualize_render` at a motion time, exploded-view factors bound to motion
channels, motion clearance) ARK resolves them in this order:

1. **Process cache**: content-addressed by the two digests, filled by every
   bake, verified re-bake, clearance re-solve and migration. Bounded to 64 MiB
   and 256 entries, least recently used first.
2. **Derived section**: on open, `ARK-MOTION-FRAMES1` entries of the `.ark`
   container's optional `ark_derived` table are accepted only when their key
   (motion name plus `graph_digest`, `source_digest` and both frame digests)
   matches a current receipt and their bytes reproduce both digests. Others are
   discarded and counted in `derived_geometry.motion_frames`.
3. **Re-bake**: the graph is solved again at the receipt's fps through the same
   joint solver. If the moving instances, frame count and both digests match,
   the frames are the recorded evidence (`verified:true`) and are cached.

A re-bake that does not reproduce the receipt (a stale assembly with no cached
frames, or a different solver build) is returned with `verified:false` and a
`mismatch` text naming the differing fields and stale reasons. It shows the
current solve and is labelled as such everywhere:

- `motion_info{name,frame}` adds `frames:{source,verified,mismatch?,stale_reasons?}`.
- `visualize_render` rejects an unverified re-bake with `VISUALIZE_MOTION_STALE`
  and reports `motion.frames` otherwise.
- Motion clearance re-solves in double precision anyway; for a receipt the
  float32 rounding must reproduce both digests, else `MOTION_BAKE_MISMATCH`
  ([motion clearance](MOTION_CLEARANCE_046B.md)).
- Studio's Baked Motions panel shows `frames memory | derived | re-baked |
  inline (legacy)` and an amber line for a mismatch.

A known mismatch is memoized per receipt, source digest and graph digest, so it
is not solved again on every panel refresh.

Saving writes the cached frames of current receipts into the derived section,
after saved native geometry and within the capacity it leaves. Frames that are
not cached, or do not fit, are omitted; the `doc_save` receipt reports
`saved_motion_frames` (`entries_saved`, `bytes_saved`, `entries_omitted`,
`entries_not_cached`). JSON exports carry no derived section; their frames
re-bake on demand.

## Existing documents

Opening a 044B-048B document never rewrites it. Its current authoring must
equal the journal head, so converting inline frames on load would need a new
journal node or a silent journal rewrite. Neither is acceptable, so the safe
option was chosen: the inline bake stays readable and playable (`source:"inline"`),
and the next `motion.bake` of that motion writes a receipt. That node's delta
erases the payload keys and adds no frame bytes. Older nodes keep theirs until
an explicit migration.

## Explicit migration

`history_migrate_derived{path,expected_revision,confirm:true,motivation?,actor?}`
and Studio's **Reclaim baked frames** button in the Persistent iterations
dialog. Contract, refusals and record: [iteration history](ITERATION_HISTORY.md#explicit-migration).

- **Backup.** The tool saves the complete pre-migration document beside the
  saved document as `<name>.pre-049A.ark`, or `-2` to `-99` when taken. It never
  replaces a file and refuses a document that has no saved `.ark` path. The
  backup is reopened and compared before the journal is touched.
- **Rewrite.** All states are converted and the journal is rebuilt as
  `schema:2` with new digests and node ids. Topology, branches, undo/redo
  targets and node metadata are kept. The blob dictionary keeps only what the
  new deltas reference.
- **Record.** `history_info.migrations[]` holds actor, motivation, UTC, counts,
  bytes before and after, old and new head and baseline ids and the backup.
- **Publication.** The migrated document replaces the session document only
  after it validates and reopens in memory. The revision advances by one and
  prepared candidates are discarded. Saving stays explicit (`doc_save`); Studio
  saves to the open path after the confirmed migration.

## Frame-step rejection

`MOTION_FRAME_STEP_TOO_LARGE` now states its fix: the channel, first offending
frame and time, the largest remaining step at the current fps and
`required_fps`, the smallest fps found upward from the velocity estimate that
keeps every unlimited revolute step at or below 170 degrees within 240 fps and
7201 frames. Agents had retried with guessed fps values. Before 049A a higher
fps also grew the journal by one frame copy per bake; with derived frames it
does not. See [Skeleton Motion](SKELETON_MOTION_044B.md#validation).

## Verification (local)

All results come from a local diagnosis build: llvm-mingw clang with the
unpatched MinGW OCCT 8 SDK on one Windows machine, not the release toolchain.

`tests/derived_bakes_049a_tests.cpp` (CTest `derived-bakes-049a`) uses only
synthetic fixtures. It covers:

- no frame payloads in authoring or journal JSON after repeated bakes;
- bit-identical re-bakes verified by `frames_sha256`, a tampered digest reported
  as mismatch, stale receipts, memoized mismatches;
- `motion_info` frame sources and `visualize_render` from a cold cache;
- derived-section round trips: save and reopen with entries, a JSON export
  without them, an `.ark` saved with an empty cache, and corrupt or foreign
  entries discarded;
- legacy load without rewrite and conversion on the next bake;
- migration: refusals, backup naming without overwrite, record, shrink,
  restore and undo on the rewritten journal, a second run refused, save and
  reopen;
- motion results unchanged between the inline path and the receipt path
  (frames, diagnostics, status, decoded frames and bake summary);
- 046B clearance results unchanged between the two paths on the arm/bridge
  and arm/post fixtures (native build only);
- the frame-step fix.

It passed with 187 checks. `motion-graph-044b` was updated for receipts and
format 49 and passed with 353 checks. The full local CTest passed 150 of 152.
The two failures, `native-solid` and `native-surface`, also fail on the
unmodified 048B tree with this unpatched OCCT. In one parallel run
`agent-benchmark` failed with "Resident OCCT worker exited". It passed alone and
in a second full run.

These Studio selftests exited 0: `baked-motions-ui` (1280x800, 1440x900,
1366x800), `exploded-views-ui` (1280x800, 1366x800), `history-ui`, `recovery-ui`
and `assistant-ui`. The baked-motions selftest now does the following:

- builds a 044B-shaped document with three re-bakes and saves it;
- declines, then confirms, Reclaim baked frames in the iterations dialog;
- checks the backup, the schema 2 journal, the single revision and the saved,
  payload-free file with staged derived frames;
- plays from the cache, drops the cache and plays a verified re-bake.

Captures inspected: the dialog with the enabled button, the dialog with the
disabled "No baked frames to reclaim" button (assistant-ui), and the panel's
"frames re-baked" state line. The amber line for an unverified re-bake was not
captured in Studio; the unit tests cover that path. An `ark-mcp` stdio smoke
listed 107 tools, including `history_migrate_derived`, and rejected a call
without `confirm` with `HISTORY_MIGRATION_CONFIRM_REQUIRED`.

### Measurement: kiosk-like fixture

The fixture has 15 part occurrences and 3 carrier groups. A door, a screen arm
and a drawer give 11 moving parts and 3 motions (7 s, 8 s and 5 s). They are
baked at 30 fps and then re-baked at 60 to 120 fps, with a parameter change
and door re-bake every third batch. This repeats until the legacy journal
rejects an edit.

| | nodes | `document_bytes` | journal bytes | inline frame references / bytes |
| --- | --- | --- | --- | --- |
| 044B-048B writer, at the first rejected edit | 12 | 3,728,890 | 3,721,670 | 58 / 3,684,030 |
| after `history_migrate_derived` | 12 | 40,642 | 33,590 | 0 / 0 |
| 049A writer, same sequence, all accepted | 23 | 63,657 | 56,607 | 0 / 0 |

The migration took 0.9 to 1.3 s over two runs. It removed all 53 frame blobs,
and the previously rejected re-bake then committed.

The physical file is a separate matter. The legacy `.ark` was 3,903,488 bytes.
An incremental save of the migrated document over it stayed at 3,985,408 bytes,
because SQLite keeps free pages (the 045A limit). Saving the same document to a
new file wrote 561,152 bytes, including the cached frames in the derived
section. Edits are rejected by the logical 4 MiB limit, which applies to
`document_bytes`, not to the physical file size.

This is one synthetic fixture on one machine. The user's document was not
opened, copied or measured.
