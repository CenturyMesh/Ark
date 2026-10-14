# ARK — licensing and distribution

ARK's original source code and documentation use [Apache-2.0](../LICENSE),
adopted on 2026-10-12. [NOTICE](../NOTICE) records project attribution.
This file retains its existing path so documentation links remain valid.

## Source and dependency notices

The repository's license and notices are the published terms. Dependencies and
imported component assets retain their own terms and provenance. See
[third-party notices](THIRD_PARTY_NOTICES.md) for the dependency inventory and
[native build instructions](NATIVE_BUILD.md) for the pinned OCCT build.
Keep the applicable notices with redistributed packages.

## Distribution evidence

Milestone **A.05** tracks dependency notices, software inventory, binary
provenance and distribution review. **F.02** tracks public licensing and
distribution guidance. Retaining notices does not establish completion of the
entire distribution audit.

The native worker can be selected through `ARK_OCCT_WORKER`; keep its runtime
and OCCT libraries compatible. Corresponding-source delivery and the complete
modified-library use route remain part of the distribution audit. The current
[third-party notices](THIRD_PARTY_NOTICES.md) describe that boundary.

Runtime identity comes from [release.json](../release.json). The
[update contract](UPDATES.md) documents integrity checks, staging, activation
and rollback, with online transport qualification recorded separately.
User drawings, sessions, credentials and private component databases are
excluded from release content by default.

## Providers and contributions

Model access and account credentials are configured separately from CAD
authoring; see [connecting agents](CONNECTING.md). The
[usage display](USAGE_PRICING.md) reports provider usage and available rates
with its documented limitations.

See [CONTRIBUTING](../CONTRIBUTING.md) for changes and verification,
[SECURITY](../SECURITY.md) for vulnerability reports, and the
[public roadmap](ROADMAP.md) for technical acceptance gates.
