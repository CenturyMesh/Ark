# 056C project storage and command discovery

Milestones **A.02 / A.04 / A.05 / D.02**: **Verificado no escopo** for the
local storage and command contracts below. Large-project, distributed-writer,
deployment and production qualification remain **Parcial**. This work does not
change the semantic `.ark` document version or declare roadmap completion.

## Storage

The technical compression work from PR #35 is integrated on the newer 055D
code base without adopting its older release identity. The physical container
still uses version 2 for optional derived data. One Zstandard 1.5.7 frame shares
repeated bytes across native geometry and baked-motion entries. Canonical large
base64 values travel as decoded bytes and reconstruct exactly; B-rep identities,
native validation and reference authority remain unchanged. Legacy per-entry
rows load, and readers without pack support discard the optional cache and
rebuild from authoring. Full format rules are in [FORMATS.md](FORMATS.md).

The stored derived limit remains 128 MiB and the physical file limit remains
256 MiB. The separately bounded decoded limit is 256 MiB, tested with a 144 MiB
synthetic round trip. Unchanged valid packs avoid recompression. Reuse verifies
the decoded manifest as well as row hashes, so a valid compressed frame with a
relabelled manifest is repaired on save. Stored-capacity omissions retain a
bounded document-order prefix and are reported separately from saved authoring.

SQLite compaction uses transactional `VACUUM` when a save frees at least 1 MiB
and a quarter of the file. It preserves SQLite locking and existing connections
instead of publishing a stale snapshot after closing the source connection.
Maintenance failure retains the preceding committed save and reports its cause.
This does not add a cross-process authoring merge contract.

The logical document/journal limit remains 4 MiB; features remain limited to
1024 and global parameters to 256. Current scoped-sketch support already allows
256 sketches per scope and 4096 in total, with aggregate geometry resource
limits. Reusing a feature definition through assembly instances remains the
supported way to avoid repeated authored definitions. No cap is lifted merely
because storage compresses well. Compression runs on CPU; it is not a claim of
GPU simulation or arbitrary project capacity.

Zstandard's original source/license files and provenance are retained. Windows
and Linux packaging carry its notices and the release SBOM includes the direct
dependency. ARK's own license is unchanged.

## Parameterized command palette

`agent_onboard` has a `commands` topic. It searches the actual current tool
registry and six parameterized recipes without expanding a document inventory:

```json
{"topic":"commands","query":"bom","limit":3}
{"topic":"commands","command":"tool:library_source_page"}
{"topic":"commands","command":"parameter_set"}
```

Search results include a stable command ID, summary, kind and explicit truncation
status. An exact `tool:<name>` returns the tool's current input schema. In the
Studio assistant it also exposes just that specialist tool on the next request,
provided the role's existing tool catalogue permits it. Topic loads and exact
selections append in first-load order. Deferred-provider tool additions use the
first actual loading call, including tools shared by BOM, vendor and component
topics; any preloaded alias makes its tools available.

The recipes are `parameter_set`, `native_box`, `feature_parameters`,
`assembly_instance`, `material_density` and `revision_status`. A recipe publishes
its parameter schema and ordinary tool-call template. Supplying `parameters`
returns `prepared_call` with `executed:false`. Mutation recipes prepare
`doc_preview`, require an explicit observed `expected_revision`, and preserve
expression strings. Missing/unknown/type-invalid parameters reject. Preparing
a recipe does not evaluate geometry, preview, reserve a target or mutate the
document. The caller executes the returned tool, inspects its candidate and
explicitly applies it through the existing host path. Stale revisions reject
without silent rebasing. This is an agent command palette, not a new Studio
keyboard command-bar control.

## Local evidence

Windows Release build, Clang/LLVM-MinGW 23.1.3 and patched OCCT 8.0.1; public
synthetic fixtures only. Measurements below compare identical inputs and
criteria. They are serialized-byte/storage observations, not provider token,
cost or end-to-end latency claims.

| Observation | Result |
| --- | --- |
| Eight synthetic derived entries, exact byte round trip | 1,665,402 decoded bytes; 645,659 bytes in one pack versus 1,246,094 in independent packs |
| Same joint pack, one local measured run | Encode 21.59 ms; decode 5.09 ms; no general performance qualification |
| Synthetic container after deleting optional cache | 18,915,328 to 20,480 physical bytes; authoring reload verified |
| Highly repetitive synthetic decoded-capacity fixture | 150,994,944 decoded bytes to 4,833 stored bytes; exact round trip; not a realistic CAD compression ratio |
| Full tool documentation versus BOM search, limit 3 | 183,120 versus 1,667 serialized bytes |
| Assistant offered tools | Core 99,687 bytes; one selected source-page tool 100,551; vendor topic 104,111 |
| Command catalog contracts | 458 checks, including native box preview/apply, all registered tool schemas, parameterized execution, stale rejection and role filtering |
| Derived pack/container contracts | 142 checks, including corrupt/truncated/oversized frames, legacy migration, wrong manifest, exact order, stored-capacity omission, chunk loss, transactional compaction and existing connection visibility |

The following ten selected CTest suites passed: `agent_contracts-032d`,
`container_snapshot-031b`, `persisted_geometry-031b`, `document-container`,
`history-compaction-051a`, `derived-pack-051a`, `command-catalog-056c`,
`assistant-connector`, `agent-tool-layer-042d`, and `distribution-fixtures`.
`release_evidence_tests.ps1` also passed notices, SBOM, package allowlist,
private-design rejection, hashes and provenance checks. The first selected run
passed nine suites and failed distribution setup because `cmake` was absent
from PATH; that suite passed after adding the configured CMake directory.
No hosted CI, Linux runtime test, installer/OTA publication or live model task
comparison was performed by this storage/commands workstream.

Local logs are generated under `out/scale-*.log` in the worktree; they are not
bundled in product packages. `code-context` was attempted but could not run
because `uv` is unavailable; symbol/contracts were confirmed in current source
with `rg` and direct reads. Historical memory was consulted and rechecked;
its old document-wide sketch-limit observation is superseded by current scoped
validation in `Document::validateSketchScopes`.
