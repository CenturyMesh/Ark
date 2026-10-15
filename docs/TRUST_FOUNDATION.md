# Trust foundation

The A.02–A.05 implementation adds local work protection and repeatable release
evidence. Acceptance remains scoped to the tests actually run for each iteration.
Existing CAD geometry, document source versions and distribution entrypoints remain
the authority of their respective contracts.

## A.02 — recovery and persistence

Recovery uses a separate integrity-checked document container. Its name binds a
normalized source identity and a per-process writer token; independent sessions
cannot replace one another's recovery. Candidate discovery retains unchosen files.
Saving refuses the source itself, aliases of it, ordinary documents and recovery
owned by a different writer. Loading validates before publishing a candidate.
The Studio integration reviews candidates explicitly and recovers into an unsaved
branch requiring Save As. Recovery does not replace a normal save.

`tests/recovery_tests.cpp` uses synthetic temporary workspaces. The document
container suite injects `SQLITE_FULL` at every observed SQLite write position in
its save fixture, checks the previous logical revision and protects JSON migration
bytes. These bounded fault tests do not emulate every filesystem, power failure or
network volume.

## A.03 — regression and release evidence

`tests/corpus/manifest-v1.json` binds repository-authored synthetic cases to SHA-256
and seed 19020. Fixtures cover accepted history/parameter authoring, stale revisions,
invalid inputs and atomic batch rejection; native cases have their separate suite.
Canonical LF bytes keep fixture digests stable across checkout platforms. No user
drawings are part of this corpus.

Build scripts retain CTest JUnit output. Workflow definitions preserve the existing
optional installer and prerelease publication gates. A local passing suite does
not establish hosted execution or publication. A failed attempt is part of the
iteration receipt and must not be represented as a passing test.

## A.04 — comparable performance

`scripts/benchmark-regression.ps1` records raw samples, median and p95 for synthetic
16/128/512-feature projects: fresh-process load with recompute, warm load,
incremental save, changed-parameter recompute and synchronous `doc_info` calls.
It retains fixture and executable hashes and machine identity. Comparison rejects
different workloads, sample counts, machines and fixture hashes. Use an equivalent
baseline and retain unsuccessful runs before claiming a performance change.
The runner first creates an invalid attempt receipt, retains it on failure and
marks the completed report valid only after all readbacks and comparisons succeed.
Retries require a new output path; incomplete attempts cannot be compared.

Fresh-process reads keep filesystem caches warm. They are not measured OS cold
cache reads. API timing does not establish viewport input, GPU or display latency.
Studio interaction measurements and true cold-cache qualification remain separate
evidence. The harness uses analytic boxes, so it does not qualify OCCT workloads.

The 029B followup also measures exact native assembly edits through Studio and
reuses unchanged viewport representations. Its matched full/reuse modes isolate
the viewport change; the analytic recompute comparison remains separate. See
[native performance scope and raw evidence](PERFORMANCE_029B.md).

The 029B `doc_info` summary computes history/evaluation eligibility once per call
and derives consumed inputs only from active consuming features. Ordered history
queries walk the prefix once instead of searching it for every row. These states
stay local to the query; there is no cache that can survive a source/revision edit.
The response retains its authored order, fields and distinct history, loading,
suppression and terminal-output semantics. See the
[029B verification](VERIFICATION_029B.md) for before/after measurements and limits.

## A.05 — SBOM and dependency review

Windows packages include CycloneDX 1.5, a source inventory and a payload inventory;
Linux workflow preparation emits sidecars for its extracted archive. The source
inventory hashes tracked and nonignored worktree bytes, records dirty state and
excludes ignored drawings/profiles/builds. It does not prove compiler inputs.
The payload census rejects unexpected files, credentials and designs. Every bundled
DLL/shared object has a runtime SBOM component and hash. Filename family, declared
source association and an observed binary FileVersion remain separate; unknown
source versions are never invented from filenames.

`scripts/audit-dependencies.ps1` binds its result to the exact input SBOM bytes and
queries supported source identities through OSV. It records pagination, returned
advisories, unavailable/malformed responses and unqueried components separately.
Release workflow preparation fails on findings and query failures. No result is
equivalent to complete indexing or proof of no vulnerabilities. Runtime-only
components lacking supported source identities remain explicitly unqueried.
Follow the triage process in [SECURITY.md](../SECURITY.md).

Archive provenance binds the archive hash and the three generated inventories.
It is an unsigned local integrity receipt, not Authenticode, an independent
attestation or reproducible-binary proof. Signing, a qualified online update bridge,
hosted evidence and manufacturing acceptance require their own observed results.
