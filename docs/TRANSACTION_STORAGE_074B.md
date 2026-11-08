# Transaction storage and validation — 074B

**A.02/A.03/A.04/D.02 — Verificado no escopo local.**

Transactions clone validated authoring through a canonical typed value instead
of writing and parsing a complete intermediate JSON string. Persistence checks
count the exact escaped JSON bytes and validate nesting/duplicates directly.
Number normalization, canonical object ordering and the existing document
decoder remain authoritative. Typed/text parity, invalid-input rejection,
destination preservation and persistent Undo/redo are tested.

Legacy Undo snapshots share immutable strings. Rollback moves the captured
stacks/candidates back and is idempotent, so a later error cannot perform a
second rollback from an already moved snapshot. Prepared publication moves the
retained history and distinguishes its preparation evidence from publication.

SHA-256 uses the platform provider for larger inputs, with byte-identical
portable fallback. Known and independent binary vectors cover both sides of
the dispatch boundary. Windows retains one process-lifetime algorithm handle;
each hash operation still destroys its own handle. A first hash from the
background memory queue during shutdown must not register a static destructor
while the main thread holds the CRT exit lock and waits for that queue.

The original shutdown test exposed that real deadlock. The same isolated child
timed out at 20.01 seconds with zero stored rows; after the lifetime fix it
exited successfully in 0.032 seconds and retained exactly one expected episode.
The queue, timeout and acceptance criteria did not change. Receipts are under
`out/verification-074b/memory-child-diagnosis`.

PNG/JPEG validation retains at most 256 small metadata receipts, never pixel
buffers. Every hit hashes the **complete current encoded source** and checks
the declared MIME, raw-source hash and dimensions against previously decoded
evidence. A caller-supplied hash is never sufficient. Changed/forged bytes or
metadata reject; optional memo retention failure does not reject a valid image.
The image fixture retained three receipts, with three decoder attempts and
588 authenticated reuses. This does not remove per-image format limits.

## Matched measurements

Same local resource settings, two warmups and five measured edits, with exact
final geometry and retained-node counts checked:

| Synthetic parameter edit | 073B | 074B final runtime |
|---|---:|---:|
| Three embedded PNGs, about 3 MiB of encoded source | 2490.50 ms | 473.62 ms |
| 1000 existing history edits | 38.38 ms | 32.12 ms |

The first typed/platform-hash checkpoint took 2047.40 ms for the image case;
decoded-source memoization alone took 1843.99 ms. The final receipt memo also
avoids decoding unchanged Base64 while continuing to authenticate all current
encoded bytes. Intermediate receipts and the initial Python fixture setup
failure remain retained. These are scoped local measurements, not a promise
of constant-time edits or a peak-memory qualification for arbitrary projects.

Reproduction and comparison: `out/performance-074b/transaction_benchmark.py`
and `storage-boolean-final.json`. Final distribution evidence is recorded
in [074B](iterations/074B.md).
