# Security

ARK is local CAD software with an optional AI assistant. Treat MCP clients,
provider endpoints and imported files as trust decisions. There is no claim of
a completed independent security audit or production security certification.

## Report a vulnerability

Use [GitHub private vulnerability reporting](https://github.com/CenturyMesh/Ark/security/advisories/new).
Include the affected commit/version, operating system, a minimal synthetic
reproduction, observed impact and relevant redacted logs. Do not attach real
credentials, customer drawings or an entire settings/profile directory.

If the private form is unavailable, open an issue asking for a private reporting
channel without disclosing exploit details. No response SLA is currently offered.
Normal reproducible bugs belong in public issues; security-sensitive details do not.

## Boundaries

- The Windows live MCP pipe is restricted to the current Windows user and rejects
  remote pipe clients. A trusted client can edit the active document and use the
  exposed file operations. MCP is not a sandbox for malicious local processes.
- `expected_revision` and atomic commits prevent stale writes; they do not replace
  authorization. A lost response does not prove a mutation failed: inspect before retrying.
- ChatGPT credentials are stored with current-user Windows DPAPI in ARK's own
  profile. ARK does not import Codex's credential store. API connector settings
  retain environment-variable names, not API keys.
- Model requests can contain the goal, selected document information, tool results
  and requested snapshots. Choose a provider appropriate for your data. Local CAD
  does not imply that an enabled hosted assistant keeps all inputs offline.
- A custom connector sends requests to its configured endpoint. Only configure
  endpoints and environment variables you trust. Untrusted models and tools may
  produce incorrect geometry or instructions; inspect results before manufacture.
- The exact B-rep and validation contract remain authoritative. Rendering and AI
  output are not evidence of manufacturing safety or certification.

## Development and releases

Never commit `.env` files, tokens, account stores, private drawings, customer
models or private activity logs. Use synthetic fixtures and explicit staging.
Keep dependency notices and pinned-source hashes. Run the relevant tests and
secret/history checks before publishing. Secret scanners have false positives
and false negatives; a clean scan is scoped evidence, not a guarantee.

Only the latest source revision is actively developed. Historical releases are
retained for reproducibility; no separate long-term security maintenance policy
has been established. Windows packages are currently unsigned; compare hashes
from a trusted release channel. See [updates](docs/UPDATES.md).

The generated CycloneDX SBOM inventories direct source dependencies and bundled
runtime binaries. Run `scripts/audit-dependencies.ps1` against that exact SBOM to
retain public OSV queries and their results; release preparation uses
`-FailOnFinding`. Database failures, unsupported identities and empty responses
retain distinct statuses. Empty responses do not establish complete indexing or
the absence of vulnerabilities.

Review each returned advisory against the pinned source, enabled code paths,
affected inputs and upstream fixes. Record the advisory ID, observed revision,
applicability evidence, corrective change and regression result. Keep unresolved
relevant findings out of a release; do not dismiss a finding solely because a
generic scanner returned no other records. Report sensitive reproductions through
the private channel above, using synthetic inputs and redacted diagnostics.
