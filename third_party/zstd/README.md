# Zstandard 1.5.7

Unmodified library sources from the official
[facebook/zstd v1.5.7 release](https://github.com/facebook/zstd/releases/tag/v1.5.7)
archive `zstd-1.5.7.tar.gz`, downloaded on 2026-10-22.

- Archive SHA-256: `eb33e51f49a15e023950cd7825ca74a4a2b43db8354825ac24fc1b7ee09e6fa3`,
  equal to the release's published `zstd-1.5.7.tar.gz.sha256`.
- Retained: `lib/common`, `lib/compress`, `lib/decompress`, `lib/zstd.h`,
  `lib/zstd_errors.h`, `LICENSE` and `COPYING`, byte-identical to the archive.
- Omitted: `lib/decompress/huf_decompress_amd64.S` (ARK builds with
  `ZSTD_DISABLE_ASM`), legacy formats, the dictionary builder, programs,
  tests and contributed code.
- SHA-256 over the sorted `sha256sum` list of the retained `.c`/`.h` files:
  `888827c2123eabb439b074d20a82a713aee766048ac592357421bb3a5d623636`.

Zstandard is dual-licensed under BSD (`LICENSE`) or GPLv2 (`COPYING`). ARK uses
the BSD alternative; both upstream files are retained unchanged. This
dependency keeps its own terms alongside ARK's Apache-2.0 code, and packages
include its BSD license and this provenance.

ARK builds it as a separate static, single-threaded target without legacy
format support. It compresses only the optional derived section of `.ark`
containers (051A); see `docs/FORMATS.md`.
