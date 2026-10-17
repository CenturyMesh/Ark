# Binary resource libraries

039D ships the six original compressed UI PNGs in `assets/ark-ui.arklib` rather
than duplicate loose PNG files. The logo and icon remain the original artwork;
the Windows executable also retains its normal compiled application icon.

The container begins with ASCII `ARKLIB01`, a little-endian 32-bit UTF-8 index
length, a JSON `ark.resource-library.v1` index and the original resource bytes.
Entries are sorted, uniquely named and cover the payload without overlap or
undeclared trailing bytes. Each declares its name, relative payload offset,
length, media type and SHA-256. The reader bounds file/index sizes and verifies
every entry before publishing the library. It rejects unknown versions, damaged
payloads, duplicates and unsafe names.

Studio caches the verified library and decodes selected image bytes into the
existing WIC/OpenGL texture path. User images remain their own user-owned inputs.
The library does not choose geometry or change `.ark` document format, and a
resource digest is not an independent publisher signature.

`scripts/build-resource-library.ps1` creates deterministic archives from the
explicit source asset allowlist. Source PNGs remain editable in the repository;
their already compressed bytes are preserved, so no additional compression or
startup-performance improvement is claimed without measurement. Binary roundtrip,
malformed input, deterministic output and packed-only UI are separate checks.
