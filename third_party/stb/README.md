# stb_image 2.30

Unmodified `stb_image.h` and `LICENSE` downloaded from the official
[nothings/stb repository](https://github.com/nothings/stb), pinned to commit
`013ac3beddff3dbffafd5177e7972067cd2b5083`.

- [Pinned header](https://github.com/nothings/stb/blob/013ac3beddff3dbffafd5177e7972067cd2b5083/stb_image.h)
- [Pinned license](https://github.com/nothings/stb/blob/013ac3beddff3dbffafd5177e7972067cd2b5083/LICENSE)
- Header SHA-256: `594c2fe35d49488b4382dbfaec8f98366defca819d916ac95becf3e75f4200b3`
- License SHA-256: `bebfe904b14301657e4e5d655c811d51fd31b97c455b9cc2d8600d6bac6cff63`

Upstream provides alternative MIT and public-domain terms. ARK uses the MIT
alternative and retains both upstream alternatives unchanged in `LICENSE` and
the header. This dependency retains its own terms alongside ARK's Apache-2.0
code; packages include its complete license and this provenance.

ARK compiles only PNG/JPEG memory decoding, with file I/O, HDR and linear-float
paths disabled. `STBI_MAX_DIMENSIONS` is 8192. The host preflights at most 16
megapixels and 4 MiB compressed bytes before full decoding. Its per-thread
decoder allocation limit is 192 MiB, including temporary decoder buffers and
RGBA output; references produce a thumbnail of at most 64 pixels per side.
Windows retains WIC as the normal decoder. The same portable decoder is used
on headless platforms and exercised explicitly by Windows offline contracts.
