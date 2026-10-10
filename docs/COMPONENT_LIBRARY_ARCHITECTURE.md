# Reusable STEP component library

The library separates searchable engineering metadata from immutable source
files and native geometry packages. Its database is local to the workstation;
the CAD document stores the inserted component's source package, rather than a
mutable link to a supplier download.

## Database choice

The selected backend is SQLite 3.54.0 compiled as C into ARK, with prepared
parameterized queries, relational indexes, FTS5 text search and WAL transactions.
SQLite explicitly supports embedded desktop/CAD catalogs; WAL permits readers
alongside a writer, while transactions still serialize writers. Keep the library
on a local disk, not a shared network filesystem.
[SQLite use cases](https://www.sqlite.org/whentouse.html),
[WAL concurrency](https://www.sqlite.org/wal.html),
[FTS5](https://www.sqlite.org/fts5.html).

Other native candidates were considered:

| Candidate | Fit for this workload |
| --- | --- |
| SQLite + FTS5 | Structured dimensions, compound filters, text aliases and transactional metadata in one embedded engine; selected. |
| LMDB | Memory-mapped ordered key/value lookups; useful for a future specialized cache, but ARK would need to maintain its own secondary/text indexes. |
| RocksDB | Native C++ persistent key/value engine; useful for write-heavy stores, but adds index/search integration work for this catalog. |

The latter comparisons are architectural choices, not a measured speed ranking.
See [LMDB's documented model](https://www.symas.com/lmdb.php) and
[RocksDB](https://rocksdb.org/). There is no universal fastest database.
ARK's benchmark must report corpus size, query types and latency on the actual
machine; geometry parsing/download time is measured separately from DB lookup.

## Identity, organization and agent workflow

1. Search locally first. Use category, exact standard, nominal thread diameter,
   thread pitch, length, material, manufacturer and part number when known.
   Text search and Portuguese/English aliases help discovery, but do not replace
   dimensional compatibility checks.
2. Acquire the original STEP and record its source page, license statement,
   supplier/reference, revision and provenance. Do not infer a standard, material
   or strength class from a filename or a visually similar shape.
3. Copy into the library and compute its SHA-256. OCCT validates the copied
   source; it must not inspect a different file that can change underneath the
   import. Multi-body sources require explicit selection or whole-component
   intent. Preserve the original bytes.
4. Index one stable source/selection identity. Exact duplicate files reuse the
   source blob; different metadata is a conflict to review rather than a silent
   overwrite. Different STEP exports of nominally identical parts remain distinct
   source artifacts until their equivalence is established.
5. Keep nominal catalog dimensions and material as declared metadata. Native
   B-rep validity, volume and bounds are separate measured geometry evidence.
   A successful parse cannot attest a standard, supplier identity or license.
6. Review and update metadata using its expected revision. Concurrent agents
   cannot silently replace each other's classification. Missing properties stay
   unknown; a missing pitch must not match an exact requested pitch.
7. Inspect the selected record and insert its exact source package using the
   current document revision. Use assembly placements for multiple copies; verify
   mating interfaces, clearance, load rating and actual supplier specification.

For example, a request for a socket-head screw should query its declared standard,
diameter, pitch and length, then compare material/grade and supplier reference.
Do not automatically substitute another standard or a coarse thread for a fine
thread because its name is similar. Reuse the returned stable component ID on
subsequent designs.

Agents use bounded library operations, never raw SQL or unrestricted FTS syntax.
Downloads and geometry evaluation happen outside the short metadata transaction.
Content hashes prevent filenames and aliases from becoming geometry authority.

## Sources and acquisition

`library_sources` lists source capabilities. Public KiCad discovery lists
categories and STEP entries by directory/page; it does not imply a global search
when only one page was inspected. `library_fetch` downloads a direct public
HTTPS STEP under byte/time limits, then uses the same validation and ingestion
path as a local file. No browser credentials or account tokens are sent.

KiCad's 3D library uses CC-BY-SA 4.0 with its library exception. Redistributing
a library collection has attribution/license obligations even though use in an
electronic design has an exception. Keep the source's credit/license references;
community models are not supplier tolerancing evidence.
[KiCad library license](https://www.kicad.org/libraries/license/).

TraceParts has an official CAD request API, including part/configuration inputs
and authenticated access. CADENAS 3Dfindit also supplies configurable manufacturer
catalogs. These are registered as provider/account integrations, not silently
scraped or treated as unrestricted bulk-download sources. Authorized downloaded
files can be ingested locally.
[TraceParts API](https://developers.traceparts.com/reference/post_v3-product-cadrequest),
[3Dfindit catalogs](https://www.3dfindit.com/en/corporate/ecatalog/).

The first implementation does not promise automatic engineering recognition of
arbitrary STEP files, supplier inventory, pricing or universal manufacturing
qualification. It provides reproducible acquisition, exact source custody and
fast explicit retrieval so the same verified geometry can be found again.
