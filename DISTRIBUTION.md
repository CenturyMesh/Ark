# Windows distribution — 014F

This iteration prepares **A.05**, **F.03** and **F.05**: dependency notices,
standalone installation and preview downloads. The code is prepared; native
compilation, installer execution and release publication require a Windows runner.
The complete milestones remain partial/planned as recorded in the roadmap;
this does not claim a qualified pilot or a signed commercial release.

## Download and run on another PC

Once **Build Windows installer and publish download** completes successfully,
open [Releases](https://github.com/CenturyMesh/Ark/releases) and choose:

- `ARK-014F-windows-x64-setup.exe`: install for the current user and open ARK
  from the Start menu; the desktop shortcut is optional.
- `ark-windows-x64-0.9.15.zip`: extract the entire ZIP, then run `ARK.cmd`.
  `Install-ARK.cmd` also prepares the per-user baseline and desktop shortcut.
- `SHA256SUMS.txt`: SHA-256 hashes of those two assets.

Windows 10/11 x64 and an OpenGL 3.3 graphics driver are required. The package
contains the Visual C++ and OCCT DLLs, UI assets, contracts and dependency notices;
the destination PC does not need Python, Visual Studio or the OCCT SDK.
Modeling does not require an AI account; agents require a configured provider.

The setup uses `%LOCALAPPDATA%\ARK\app` and the existing versioned launcher.
Uninstall removes application runtimes/launcher, while documents and settings
outside that app directory remain. Installation of a newer runtime stages it
for the next normal launch. Do not save drawings in immutable `app\versions`.

Assets are attached to a versioned GitHub **prerelease**, rather than committed
as binaries into source history. Access to this private repository is required
for downloads. No public repository, source license or public binary mirror is
created. Future versions must advance `release.json`; the workflow refuses to
replace an existing release tag.

## Build locally or through GitHub

For a **personal test using binaries already built in VS Code**, use this command
from this branch's checkout. It skips rebuilding and all test suites, packages the
existing runtime, and uploads a preview Release directly through GitHub CLI:

```powershell
./scripts/publish-test-build.ps1 -Publish
```

The resulting ZIP contains the executable and its DLLs plus `ARK.cmd` and
`Install-ARK.cmd`. This path does not use GitHub Actions. GitHub CLI must already
be installed and signed in (`gh auth login`). To also create and upload a single
setup EXE, install Inno Setup 6.3+ and add `-Installer`:

```powershell
./scripts/publish-test-build.ps1 -Installer -Publish
```

`-RuntimeDirectory` selects another existing build folder. Omit `-Publish` to
prepare the files locally. The compiled iteration/version are preserved even
when older than the packaging checkout, and an existing Release is never replaced.
The Git tag targets the packaging checkout; binary source provenance is not
attested by this personal test path. Release notes explicitly say that no build
or test suite was run. No production gate or Actions billing override is needed.

The workflow runs on a push to `codex/windows-distribution`, a `v*` tag, or
**Actions → Build Windows installer and publish download → Run workflow**.
Manual runs can disable publication and keep the result as an Actions artifact.
Tags must match the numeric version in `release.json`. All releases are previews.

Local equivalent, on Windows with VS 2022, CMake and Inno Setup 6.3+:

```powershell
./tests/distribution_tests.ps1
./scripts/build.ps1 -Test -Jobs 2
./scripts/package-release.ps1 -SkipUpdateManifest
./scripts/build-installer.ps1 -PackagePath out/releases/ark-windows-x64-0.9.15.zip
./scripts/test-distribution.ps1 -PackagePath out/releases/ark-windows-x64-0.9.15.zip -InstallerPath out/releases/ARK-014F-windows-x64-setup.exe
```

`-InnoCompiler` can select `ISCC.exe`. The Windows 2022 hosted image includes
Inno Setup; no third-party download script is executed during packaging.
Use a new output directory for another build of the same identity rather than
silently replacing an existing archive.

## Verification and limits

Fixture tests exercise packaging, DLL/notice inclusion, exclusion of local
drawings, standalone installer script paths containing spaces, runtime receipts,
repeat installation and preservation of external drawings. Their dummy binaries
do not prove native execution. The Windows workflow additionally runs all native
CTest cases, creates/exports an exact box through packaged MCP with the build SDK
removed from PATH, silently installs to an isolated root, validates receipts and
uninstalls. The real interactive UI still needs a trial on the destination PC.

The initial latest Windows jobs inspected on 2026-10-12 failed with no executed
steps. [Earlier infrastructure evidence](docs/VALIDATION.md#github-actions-availability)
records a payment/spending-limit restriction. No billing setting is changed by
this implementation. If a runner cannot start, no executable or Release is produced;
the same scripts can be run on a Windows build PC.

The self-update adapter still trusts the separate, unpublished `Ark-Releases`
placeholder. The private-source download workflow deliberately uses
`-SkipUpdateManifest` and publishes no manifest for that unavailable feed.
Existing transport trust and URL checks are preserved. Authenticode signing,
online self-update and cross-PC interactive qualification remain outstanding.

Dependency instructions: [OCCT build](docs/NATIVE_BUILD.md),
[third-party notices](docs/THIRD_PARTY_NOTICES.md),
[versioned launcher](docs/UPDATES.md).

Local verification on 2026-10-12 (Linux, PowerShell 7.5.4): 22 distribution fixture
checks and 26 release identity cases passed; PowerShell and workflow syntax passed.
The existing Windows-targeted OTA suite reached an adversarial ZIP-size case that
was not rejected under Linux/.NET; its extraction code is unchanged in this work.
No Windows native CTest, Inno compilation or installed Studio execution is claimed
until a Windows runner executes the gated workflow.
