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
