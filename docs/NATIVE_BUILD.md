# Native OCCT build for ARK

ARK's exact solid and STEP backend uses the C++ Open CASCADE Technology SDK.
The reproducible dependency build is [scripts/build-occt.ps1](../scripts/build-occt.ps1):

The default `scripts/build.ps1` invokes this dependency script automatically
when its native SDK is absent, then configures ARK into `out/native-release` with
Visual Studio 2022 x64 and `OpenCASCADE_DIR` set to the installed package.
Use `scripts/build.ps1 -Test` to build and run the native C++ test suite.

```powershell
pwsh -File scripts/build-occt.ps1 -Jobs 4
```

Windows PowerShell also works (`powershell -File scripts/build-occt.ps1 -Jobs 4`).
The script downloads the official `V8_0_1` source archive, verifies its pinned
SHA-256, configures the x64 Visual Studio 2022 generator, builds Release DLLs,
and installs them under the ignored `out/deps/occt/install-msvc` directory.
It uses no Python interpreter or binding. `-ConfigureOnly` stops before compilation.
The source, build, and download cache remain under ignored `out/deps/occt`.

The installed CMake package is
`out/deps/occt/install-msvc/cmake/OpenCASCADEConfig.cmake`; pass its directory
as `OpenCASCADE_DIR` when configuring ARK with the same Visual Studio 2022 x64
generator. Use `find_package(OpenCASCADE REQUIRED)` so OCCT imports all of the
target groups required by the STEP dependency closure. Set C++17 or newer for
all consumers; ARK uses C++20. The headers are under `install-msvc/inc`, Release import libraries
under `install-msvc/win64/vc14/lib`, and Release DLLs under
`install-msvc/win64/vc14/bin`. Put that DLL directory on `PATH` for native
applications and tests, or copy the required DLLs alongside a produced `.exe`.
Do not mix these MSVC libraries with the existing MinGW build in `out/sketch`.

The configuration builds FoundationClasses, ModelingData, ModelingAlgorithms,
and the `TKDESTEP` toolkit with its dependencies. Full visualization and Draw
modules are not requested; the STEP dependency closure still includes some
visualization/application-framework libraries. Optional Tcl/Tk, FreeType,
OpenGL, TBB, RapidJSON, and vcpkg dependencies are disabled. This is a headless
modeling and STEP exchange SDK; other exchange
formats can be added through `BUILD_ADDITIONAL_TOOLKITS` in the script when
needed.

The installed SDK was checked with an independent MSVC C++20 program that
linked `TKDESTEP`, `TKPrim`, `TKBRep`, and `TKTopAlgo`. It built a box, verified
the B-rep, wrote a STEP file, then read it back successfully.

The source is [Open-Cascade-SAS/OCCT release V8_0_1](https://github.com/Open-Cascade-SAS/OCCT/releases/tag/V8_0_1),
tagged `b8f597c`. The archive SHA-256 is
`7C033D917EE8F040C0512D289DCC5F02C148889D5BAC17C3E25639ACCB44F0DA`.
OCCT reports version `8.0.1` in its CMake cache and `Standard_Version.hxx`.
The source distribution includes [LGPL 2.1](https://github.com/Open-Cascade-SAS/OCCT/blob/V8_0_1/LICENSE_LGPL_21.txt)
and the [OCCT LGPL exception](https://github.com/Open-Cascade-SAS/OCCT/blob/V8_0_1/OCCT_LGPL_EXCEPTION.txt).
Keep the license and exception with redistributed binaries and review the
terms for an eventual packaged ARK release.

## VS Code

The repository includes the `native` CMake configure preset and
`native-release` build/test presets. After the first `scripts/build.ps1` run
installs the SDK, select that preset in CMake Tools. The workspace C++ language
configuration uses CMake Tools as its provider, so IntelliSense receives the
actual MSVC compiler flags and OCCT include directories. No Python interpreter
or Pylance import suppression is configured. Legacy Python bridge/test files
have been removed rather than retained with unresolved imports.

The live Studio process test is opt-in because it requires a working OpenGL
3.3 desktop. Run `ark-studio-live-tests.exe` directly with the Studio/MCP paths
and repository root, or configure `-DARK_TEST_STUDIO_LIVE=ON` to register its
CTest case. Headless native geometry tests do not require a visible desktop.

## Linux CLI, MCP and native worker — G.04

Linux builds the C++ core, standalone MCP stdio server, CLI, test executables,
and exact OCCT worker. `ark-studio` and its live document bridge use Win32 and
are not Linux targets. Windows authentication, CUDA transport and the packaged
Windows update manager are also outside this Linux runtime. Native modeling
uses an isolated one-shot worker on Linux; Windows additionally supports the
resident event transport. The modeling contracts and `.ark` format are shared.

On Ubuntu 24.04, install build tools and run:

```sh
sudo apt-get update
sudo apt-get install -y build-essential cmake ninja-build curl ca-certificates unzip patchelf
bash scripts/build.sh --test --jobs 4
bash scripts/package-linux.sh
```

`build.sh` invokes `build-occt.sh` if the native SDK is absent. Both use the
same `V8_0_1` source archive and SHA-256 as the Windows script, with STEP and
IGES toolkit closure and optional visualization dependencies disabled. Linux
also explicitly disables `USE_XLIB`; OCCT otherwise enables it by default
for the TKService library pulled into the STEP dependency closure.
Installed OCCT shared objects use `$ORIGIN` to resolve sibling dependencies;
this avoids requiring a global loader configuration or `LD_LIBRARY_PATH`.
Downloads, SDK source/build/install and ARK outputs stay under ignored `out/`.
Set `ARK_BUILD_ROOT` to an isolated absolute directory to move all build and
dependency outputs. `OpenCASCADE_DIR` can select an existing OCCT 8 SDK.
`bash scripts/build.sh --core-only --test` verifies the portable targets
without claiming native geometry support. `--configure-only` stops before
compiling ARK; `build-occt.sh --configure-only` stops before compiling OCCT.

Alternatively, after SDK installation, use the `linux-native` configure and
`linux-native-release` build/test presets. `linux-core` and
`linux-core-release` provide the core-only configuration. Native CI runs on
Ubuntu 24.04 and packages the tested CLI/MCP/worker runtime; registering that
workflow is separate from evidence that a hosted run has completed.

`package-linux.sh` prepares `out/releases/ark-linux-x64-headless-<version>.tar.gz`
and its SHA-256 file. It uses CMake-generated identity from `release.json`,
copies only an explicit runtime/contract/notice list, and keeps OCCT shared
libraries replaceable in `lib/`. ELF runtime paths are relative to the
extracted `bin/` and `lib/` directories. User drawings, sessions, databases
and private settings are not included. Preserve the entire extracted tree.
The package README records the build platform and system runtime requirements;
compatibility with older distributions requires its own validation.

Run `bin/ark-mcp` from the extracted package for standalone MCP operation.
It owns its document and requires the normal `expected_revision` contract.
Configure an external client with that absolute executable path. There is
currently no Linux graphical Studio or Linux OTA channel in this package.

### Local Linux verification — 016F

On 2026-10-12, iteration **016F / 0.9.17** was compiled in an isolated Ubuntu
24.04.5 LTS x64 Docker container on the local WSL2 kernel, using GCC 13.3,
CMake 3.28.3 and Ninja 1.11.1. OCCT 8.0.1 was built from the pinned source.
The final native CTest run passed **65/65** in **89.33 seconds**; the final
core-only run passed **42/42** in **16.32 seconds**. These are local tests,
not a completed hosted CI run or Linux graphical Studio qualification.

Two dependency configuration failures were observed and corrected. The first
OCCT build inherited `USE_XLIB=ON` and failed on a missing X11 header. After
disabling X11, all native ARK targets compiled, but the first native test run
had 35 startup failures because OCCT's shared objects lacked a sibling-library
runtime path. Setting `CMAKE_INSTALL_RPATH=$ORIGIN` in the dependency build
resolved that loader failure; the final full native suite then passed.
No geometry tolerances, fallback shapes or document format changes were needed.

`ark-linux-x64-headless-0.9.17.tar.gz` was extracted and tested in a second clean
Ubuntu 24.04 container without the source tree, build SDK or loader overrides.
`tests/linux_package_smoke.sh` verified MCP's generated 016F identity, OCCT
8.0.1, a 6,000 mm³ exact box, STEP export with independent producer readback,
document save/load, dependency/license notices and absence of `.ark` drawings
or private settings in the package. All passed. The ELF inventory requires
`GLIBC_2.38`, `GLIBCXX_3.4.32` and `CXXABI_1.3.13`; other distributions need
separate runtime validation.

Local receipts and logs are under ignored `out/linux-evidence/`, including
`verification-016F-linux.json`, `native-loader-test.log`, `core-final-test.log`
and `package-smoke.log`. The prepared archive is 32,693,215 bytes, with SHA-256
`516cd058676c4aa5b5beb3d6b5f12e13f05dfd9d630c11d300937d3fc299509f`.
This verifies **G.04** for the Linux headless/native scope and **A.01** package
identity. Broader distribution/security gates under **A.05** remain separate.
