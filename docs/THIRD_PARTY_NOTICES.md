# ARK attribution and third-party notices

Created by MeshSphere. Project lead: Bruno Santos.

ARK's own source-code license and publication terms are **undecided**. The
commercial strategy is a proposal, not an adopted license. This document does
not grant an open-source license to ARK code or artwork. It records the retained
dependency notices for milestone **A.05**; the broader commercial distribution
audit remains **Parcial**. Keeping notices is not evidence that all distribution,
source availability, modification or asset rights obligations have been closed.

## Actual technology

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
These terms apply to OCCT separately from ARK's undecided own license.

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
No new artwork license or third-party rights clearance is asserted here.

Imported customer models, local `.ark` drawings, sessions, settings and private
component databases are not release content by default. Purchased or downloaded
component models retain their own source and licensing metadata.

Primary official dependency documents were checked on 2026-10-11. Exact full
license texts in the package remain authoritative over these short descriptions.
