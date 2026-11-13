# Incremental development builds — 069D

**A.04 / D.02 — Verificado no escopo local** for build planning and pinned-asset
stability. Development receipts remain separate from distribution qualification.

Keep one compatible build directory between edits. An iteration change updates
the generated identity; it does not require a fresh directory or a fresh OCCT
SDK. CMake and MSBuild retain the dependency graph and rebuild changed inputs.
Use a distinct output directory when packaging a new public iteration.

```powershell
# The normal application, without building every test executable.
./scripts/build.ps1 -Target Both -BuildDirectory out/native-release -Jobs 8

# Build Studio and the executable targets required by these exact CTest names.
./scripts/dev-build.ps1 -Target Studio -BuildDirectory out/native-release `
  -TestRegex '^(surface_quality_068b|studio-surface-quality-ui)$' -Jobs 8

# Work on a core/native module without compiling the Studio translation unit.
./scripts/dev-build.ps1 -Target Tests -BuildDirectory out/native-release `
  -TestRegex '^(surface_quality_068b|surface_network_host_068b)$' -Jobs 8

# Existing complete build/test path remains available.
./scripts/build.ps1 -Test -BuildDirectory out/native-release -Jobs 8
```

`-Target` supports `Studio`, `MCP`, `CLI`, `Both` and `Tests`; the last requires
an explicit `-TestRegex`. Providing a regex implies verification. The existing
`-Verify` switch without a regex retains its `product-tools-030d` smoke check.
The caller chooses tests appropriate to the change; this is not automatic
proof that all affected behavior was covered. Studio tests require the existing
`ARK_TEST_STUDIO_LIVE=ON` configuration and a usable desktop.

`-Jobs` controls concurrent MSBuild projects. `-CompilerJobs` controls MSVC
translation units inside each project and defaults to **4** (explicit range
1–16). Their parallelism can multiply; reduce either when memory is limited.
The helper adds `/MP4` only for its build subprocess when `CL` has no `/MP`
option. An existing `/MP` or `/MPn` is preserved and recorded as an override;
the caller's original environment is restored on success and failure. The
receipt records requested and effective compiler settings. `-CompilerJobs`
applies to the targeted development path, not the unchanged full build path.
Changing compiler flags can cause a first rebuild; subsequent invocations reuse
the same flags and compiled objects.

Selection uses CTest's own regex engine and configured test inventory. Only the
selected executable targets and their declared dependencies are built. Unbuilt
test executables are resolved from CMake's generated literal Release command,
then checked against its generated project. Empty selections, disabled tests,
ambiguous commands and unsupported script/external launchers fail explicitly.
Build those launchers explicitly and run CTest directly when required.

The helper validates the cache's source directory, Visual Studio 2022/x64
generator, native OCCT configuration and regeneration setting. Configuration
inputs and `release.json` are fingerprinted; changes force configuration before
test discovery. The pinned OCCT SDK remains verified on every invocation.
No cache is silently deleted, moved, substituted or accepted from another repo.

Each invocation writes a `dev-build-*.json` receipt and log in the build
directory, including configuration reasons, exact selected tests and build
targets, stage times, source digest and executable hashes. Source changes during
the invocation reject its stable source/binary claim. Failed stages retain a
failed receipt. A successful development build does not replace installer,
upgrade, geometry or publication checks.

Pinned memory assets still undergo the same SHA-256 verification. Preparation
now writes extracted runtime, license, notice and manifest bytes only when they
differ, preserving unchanged file timestamps.

## Local observations

The existing 068B `ark-cli` no-op build took **1,094 ms**, with no C++
compilation. This establishes that targeted incremental checking was already
cheap; it is not a before/after claim for compiler speed. Building every target
after a shared-library edit also relinks unrelated test executables, and a fresh
build directory loses all compiled-object reuse. The targeted workflow avoids
requesting that extra work during a focused edit.

On the final 069D source, `Both` plus four selected tests built six executable
targets in **4,173 ms** with no C++ compilation and no configure stage. The
four tests passed in 10,770 ms; the complete verified invocation took 18,068 ms,
including pinned dependency checks and source/artifact hashing. Its receipt is
`dev-build-20261107T143334440Z.json` in the reused build directory. The direct
`ark-cli` no-op check took 974 ms. These are individual local observations, not
an equivalent-workload claim that all rebuilds became faster.

On the same prepared memory assets, the original preparation rewrote two files
(`manifest.json` and `MODEL-NOTICE.txt`); the new preparation rewrote **zero**.
Measured times were 465 ms and 466 ms, respectively: timestamp stability improved,
with no measured preparation-speed gain. Receipts are under the local build's
`build-measure-069d` directory.

The helper contract suite covers matching and changed configuration, foreign
source/generator/platform rejection, disabled regeneration, changed SDK,
selected/shared/unbuilt test targets, unsupported and conflicting commands,
disabled tests, scoped compiler flags/restoration, pinned-byte preservation and
script syntax. These checks do not
claim shorter compilation of Studio's large `viewer.cpp` translation unit.
