# ARK attribution and third-party notices

Created by MeshSphere. Project lead: Bruno Santos.

ARK's original source code and documentation are licensed under **Apache-2.0**,
adopted by the project owner on 2026-10-12. See [LICENSE](../LICENSE) and
[NOTICE](../NOTICE). Separately identified third-party material retains its own
terms. This document records dependency notices for milestone **A.05**; retaining
notices is not a claim of an independent legal audit or manufacturing certification.

## Actual technology

039D adds local semantic memory using the CPU ONNX Runtime 1.23.2 (MIT) and
the Apache-2.0 DistilUSE multilingual sentence model with its pinned Dense
projection. Original model cards, attribution, runtime license and runtime
third-party notices accompany the payload under `assets/memory` and `licenses`.
`scripts/prepare-memory-embeddings.ps1` pins download bytes and source revisions;
the asset manifest and package SBOM identify model/runtime files separately.
The runtime performs local inference without an external embedding service.
These dependencies do not change the geometric authority of OCCT/B-rep.

The CAD core and Studio UI use C++20, Win32 and OpenGL 3.3 with GLSL shaders.
Open CASCADE Technology (OCCT) produces native exact B-reps, validation and
STEP/IGES exchange through the isolated C++ worker. The viewport displays a
tessellation. SQLite is a C library, statically linked for the local component
library and full-text search. The update manager and packaging use PowerShell.
Windows APIs and the installed graphics driver are platform services.

The optional GPU path tracer uses OpenGL shaders and a software BVH. This does
not claim DXR, OptiX or hardware RT-core acceleration. Rendered images and sampled
polygon triangulation do not replace exact B-rep authority or manufacturing
qualification.

## Open CASCADE Technology 8.0.1

ARK makes use of facilities provided by Open CASCADE Technology software.
OCCT is distributed under GNU LGPL version 2.1 with the Open CASCADE exception
(version 1.0). The pinned source retains `LICENSE_LGPL_21.txt` and
`OCCT_LGPL_EXCEPTION.txt`; the package includes both texts without alteration.
These terms apply to OCCT separately from ARK's Apache-2.0 license.

ARK's versioned source changes, when enabled, are identified by
`third_party/occt/patches/8.0.1/patchset.json`. That manifest records each diff
and every modified source/header's original and resulting SHA-256. Modified
OCCT files retain the upstream LGPL/exception terms and original notices;
ARK's license does not replace them. The corresponding pinned archive plus
exact diffs and declared SDK build route are packaged as
`licenses/OCCT-ARK-patches.txt`. SDKs, runtime binaries and that notice receive
separate hashes; a source declaration alone does not attest compiler inputs,
ABI compatibility or geometric qualification. See [native build](NATIVE_BUILD.md)
for separated source/build/install directories and the pending/verified gates.

- [Official OCCT license](https://occt3d.com/dev/doc/overview/html/occt_public_license.html)
- [Pinned V8_0_1 source](https://github.com/Open-Cascade-SAS/OCCT/tree/V8_0_1)
- [Pinned source archive](https://github.com/Open-Cascade-SAS/OCCT/archive/refs/tags/V8_0_1.zip)
- Archive SHA-256: `7c033d917ee8f040c0512d289dcc5f02c148889d5bac17c3e25639accb44f0da`
- ARK build instructions: `scripts/build-occt.ps1` and `docs/NATIVE_BUILD.md`.
- External worker selection: `ARK_OCCT_WORKER`, described in
  `docs/COMMERCIAL_STRATEGY.md`. Corresponding-source delivery and the complete
  modified-library use route remain part of the distribution audit.

The pinned build disables OCCT visualization, FreeType, TBB, RapidJSON and Tk.
Those libraries must not be described as bundled solely because OCCT can use
them in other configurations. Verify any future SDK change against the actual
linked DLL inventory and its own dependencies.

## Mapbox earcut.hpp 2.2.4

ISC license. Copyright (c) 2015, Mapbox. Retained unchanged in
`third_party/mapbox/LICENSE`, packaged as `licenses/mapbox-earcut-ISC.txt`.
ARK uses this header for sampled polygon triangulation. The full permission
and warranty notice accompanies the package.

- [Official pinned license](https://raw.githubusercontent.com/mapbox/earcut.hpp/v2.2.4/LICENSE)
- Provenance and hash: `third_party/mapbox/README.md`.

## SQLite 3.54.0

The original SQLite deliverable code is dedicated to the public domain by its
authors. ARK retains the upstream source notice and archive provenance in
`third_party/sqlite`. The package includes `sqlite-provenance.txt` and the
unmodified upstream `sqlite3.h` notice/declarations. No SQLite server or separate
SQLite DLL is required by this build.

- [Official copyright statement](https://www.sqlite.org/copyright.html)
- [Official amalgamation archive](https://www.sqlite.org/2026/sqlite-amalgamation-3540000.zip)
- Archive SHA3-256: `7b670a62fdfbd672b75fef004cb703c8a3e87d3a5cc7d675b4a08337004a2d93`.

## stb_image 2.30

ARK uses the official Sean Barrett/nothings `stb_image.h` memory decoder for
portable PNG/JPEG user-image attachments. Windows retains WIC as its normal
decoder; offline contracts also exercise this portable path. The host disables
file I/O and unrelated image/HDR formats and bounds input dimensions, decoded
area and decoder allocations.

The upstream MIT alternative applies to this dependency. The complete unchanged
upstream dual-license text is retained in `third_party/stb/LICENSE` and packaged
as `licenses/stb-image-LICENSE.txt`; the header retains its embedded license.
This dependency's license remains separate from ARK's Apache-2.0 license.

- [Official pinned header](https://github.com/nothings/stb/blob/013ac3beddff3dbffafd5177e7972067cd2b5083/stb_image.h)
- [Official pinned license](https://github.com/nothings/stb/blob/013ac3beddff3dbffafd5177e7972067cd2b5083/LICENSE)
- Provenance and exact hashes: `third_party/stb/README.md`.

## Zstandard 1.5.7

ARK compresses the optional derived section of `.ark` containers (saved native
geometry and baked frames) with the Zstandard library, built from unmodified
upstream sources as a static single-threaded target without legacy formats.
Authoring is not compressed by it.

Zstandard is dual-licensed under BSD or GPLv2; ARK uses the BSD alternative.
Both unchanged upstream files are retained in `third_party/zstd` and the BSD
license is packaged as `licenses/zstd-LICENSE.txt`. This dependency's license
remains separate from ARK's Apache-2.0 license.

- [Official v1.5.7 release](https://github.com/facebook/zstd/releases/tag/v1.5.7)
- Archive SHA-256: `eb33e51f49a15e023950cd7825ca74a4a2b43db8354825ac24fc1b7ee09e6fa3`.
- Provenance and retained files: `third_party/zstd/README.md`.

## Microsoft Windows and Visual C++ runtime

Windows APIs and system fonts remain subject to their vendor terms. Studio uses
Segoe UI from the installed Windows system and does not redistribute font files.
The package copies app-local Visual C++ redistributable DLLs from the licensed
build toolchain, subject to its redistribution terms. Their presence alone is
not a blanket license for other Microsoft files.

- [Microsoft redistribution documentation](https://learn.microsoft.com/en-us/cpp/windows/redistributing-visual-cpp-files?view=msvc-170)

## ARK artwork and component content

ARK's application mark, wordmark and icon atlases were generated for ARK using
ImageGen. Asset provenance and prompts are retained in `assets/ui/README.md`
and `docs/design`; the package includes that provenance as `ark-ui-assets.txt`.
The original generated ARK artwork is included under Apache-2.0 to the extent
the project can grant rights. This does not grant trademark rights or assert
exclusive copyright in AI-generated output. No third-party rights clearance
or endorsement is implied.

Imported customer models, local `.ark` drawings, sessions, settings and private
component databases are not release content by default. Purchased or downloaded
component models retain their own source and licensing metadata.

Primary official dependency documents were checked on 2026-10-11; ARK's own
license decision was updated on 2026-10-12. Exact full
license texts in the package remain authoritative over these short descriptions.
