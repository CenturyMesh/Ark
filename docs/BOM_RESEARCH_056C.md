# 056C — BOM recovery and public component research

Milestones **A.02 / D.02 / E.01**: **Verificado no escopo** for the local
regressions and the public Windows HTTPS observations below. Overall project,
supplier, manufacturing and commercial integration qualification remain
**Parcial**.

## Corrected failure and agent workflow

General web search previously attached `vendor:"web"` to supplier candidates.
Passing that observed vendor into `library_source_page` rejected the supplier
URL because the `web` policy describes the search engine's domain. Search now
returns the actual supplier policy ID in `vendor`, preserves `discovery_source`,
and supplies a concrete `next_action` with the observed URL, actual vendor and
the requested local policy root. The page call still checks the current policy;
search does not authorize a disabled supplier or cross-host redirect.

`agent_onboard{topic:"bom"}` now offers local library lookup and public sourcing
tools directly. Single-assistant completion recovery also offers these tools
from its permitted catalogue. Loading BOM does not execute a network request.

BOM recovery reports up to 32 unresolved definitions with their actual feature
IDs and required fields. For a bought item lacking source selection or supplier
identity, `research_action` points to its declared public source URL, or suggests
a search from declared manufacturer/reference fields. A description is used only
when no commercial identity was declared. These actions are suggestions, never
evidence of a matching component. Detailed review failures carry the same
structured recovery instead of only a generic rejection.

1. Inspect the counted definitions and any local component record.
2. Follow a source search candidate's `next_action`, or read an already observed
   product URL with `library_source_page`.
3. Match the actual manufacturer/reference and the requested specifications.
   Search titles and lexical matches remain unverified candidates.
4. Use revision-checked `feature.edit part_patch` to preserve existing identity
   while recording supported sourcing declarations. Keep missing claims unresolved.
5. Reinspect the current revision before `bom_review`. An old inspection or review
   is not rebased after a metadata edit.

This does not change global, detailed or limited-task completion authority.
Candidate sourcing still fails detailed completion. Unresolved representation,
material, manufacturing intent, fastening and project issues retain their
existing gates. `part` metadata remains caller-declared; a complete metadata
record does not prove a supplier match, engineering suitability or manufacture.

## Reading bounded product evidence

`library_source_page` retains the 2 MiB network resource limit, explicit redirect
authorization, untrusted-content label and full-byte SHA256/UTC receipt. The
first response still returns at most 32,000 text bytes. New read arguments are:

| Argument | Meaning |
| --- | --- |
| `query` | A literal reference/text query, 1–256 bytes; ASCII case-insensitive. Returns a window around its first match and explicit `query_found`. |
| `text_offset` | A byte offset into extracted text, up to 2 MiB; mutually exclusive with `query`. |
| `max_text_bytes` | Response window from 256 to 32,000 bytes. UTF-8 characters are not split. |
| `expected_evidence_sha256` | An observed exact page-byte hash; rejects if a later read fetched different bytes. |

`text_window` reports the total extracted byte count, actual offset, returned
bytes and `next_text_offset`. A missing query returns no matching text and
`query_found:false`; it never invents evidence. Read continuation with the prior
hash to avoid combining evidence from different pages. Dynamic HTML may change
between requests, even when its visible text is unchanged; the exact hash check
deliberately rejects that case. A targeted query obtains its evidence in one
request. There is no persistent supplier-page cache.

Public access still has no JavaScript execution, browser cookies, account
authentication or PDF text extraction. Provider challenges and unavailable data
remain explicit failures. No API adapter, price, stock, material certification,
licensed CAD download or automatic product matching is newly claimed.

## Observed verification

Local Release build with LLVM-MinGW, without the native OCCT worker:

- `component-sources`: passes the real search-to-page provider handoff using an
  injected HTTP transport, known and generic supplier IDs, policy-root retention,
  long-page reading, literal lookup, no-match results, changed evidence rejection,
  invalid bounds/hash rejection before network access and UTF-8 window boundaries.
  Existing URL, HTTP, redirect and STEP-byte rejection checks also pass.
- `bom-completion`: passes per-definition research recovery, detailed candidate
  rejection, metadata preservation, stale review rejection and required current
  inspection. Existing Single/Team completion and limited-task regressions pass.
- `ark-assistant-tests`: **553 checks passed**, including BOM source-tool exposure
  and completion-recovery tool availability; existing assistant behavior passes.
- `component-vendors-035b` and `component-library-tools`: pass their local
  policy and core tool-contract regressions.

Two opt-in native Windows HTTPS observations on **2026-10-23**, independent of
offline tests, both passed. `ark-component-sources-tests --live-web` searched
`Raspberry Pi RP2040 site:raspberrypi.com`, followed the returned page action and
read the [manufacturer's RP2040 page](https://www.raspberrypi.com/products/rp2040/)
with HTTP 200, `vendor:direct_https`, the requested reference present, and no
credentials sent. `--live-page` read that already-known page directly. The
observed resource was 373,191 bytes with 3,043 extracted text bytes; no buying or
engineering qualification was performed.

| Observation UTC | Exact resource SHA256 |
| --- | --- |
| 09:40:01, direct page | `18df3f81f482a5a0a54e4a6acea581136497f11fdc7c2b64f5d6687505962c64` |
| 09:40:02, search then page | `fc91f3c564eeba91665ab0d4d4a9d0a3daad8fde0b4c68d75af5c930f554ca46` |

These observations establish this public connector path on this machine and
date. They do not establish every supplier's availability, deployment, hosted
CI, live-model task completion or a reduction in agent cost. Native BOM/library
integration was not run in this core build and remains an integrated release check. No user
drawing was used and no `.ark` document format changed.
