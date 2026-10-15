# ARK Windows updates

ARK supports Windows x64 update verification, staging, next-launch activation and
recovery through an internal per-user launcher. The trusted GitHub channel is
`CenturyMesh/Ark`, using its stable releases and exact reviewed asset URLs. A future
website transport requires its own trusted adapter and tests. There is no
user-entered feed URL or remote installer command.

The channel switch is prepared in source. Publication of a repository or release
alone does not qualify live delivery: an older-to-newer end-to-end update trial is
still required. A GitHub **prerelease** is available for manual evaluation; the
stable updater excludes drafts and prereleases and does not offer them automatically.

Older installed runtimes and their retained launchers can still contain the retired
`CenturyMesh/Ark-Releases` placeholder. They cannot discover the new channel through
that nonexistent old endpoint. Launch a reviewed new portable package locally,
preserving drawings and settings, before relying on future checks against
`CenturyMesh/Ark`. Only this explicit local invocation can migrate the retained
launcher module; publishing source does not rewrite installed runtimes.

## Using updates

Open **ARK updates** from the Tools gallery and choose **Check for updates**.
The prepared transport reports that the update channel is not configured or
published until a compatible package is available. **Download update** verifies
and stages an offered package. Save your document and close ARK normally, then
reopen ARK. The current session is never
closed or replaced by a download.

The launcher uses `%LOCALAPPDATA%\ARK\app\versions\<version>`. A small atomic
`current.json` pointer selects the runtime; a previous runtime remains available.
Pending updates activate on the next launch. The new application must confirm
that its window, graphics resources and initial document are ready within 30
seconds, before loading a user document. If it exits before confirming,
the launcher restores and starts the previous version. If it remains open without
confirming, the previous pointer is restored for the following launch; the window
is left open so that a document cannot be terminated without saving. The timeout
applies to application initialization; loading a large user document happens after
the acknowledgement and does not cause a rollback.
An interrupted activation also restores the previous pointer on the next launch.
Use **Previous version** for a manual rollback after a confirmed update.

New unsaved drawings start in **Documents\ARK**, outside immutable version folders.
Settings remain in the existing `%LOCALAPPDATA%\ARK` location. Updates never copy
or migrate `.ark` drawings from the checkout or change the document format.
An older `ark-studio.exe` shortcut also redirects to the selected installed runtime
before opening a document. Its arguments, including Unicode document paths, are
preserved. Headless and self-test launches remain pinned to their requested build.
A newer standalone development build is not redirected to an older installation.

## Developer setup and portable launch

After building, run from the repository:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/install-portable.ps1 -DesktopShortcut
```

This optional developer command creates a baseline and an **ARK** desktop shortcut.
Distributed `ARK.cmd`, `start-studio.ps1`, and the first in-app download prepare the
internal baseline automatically; this script is not a required user installer.
Administrator rights are unnecessary. For an isolated test, pass `-InstallRoot`.
The stable `ark-launch.ps1` retains its activation and startup-ACK protocol. An
explicit invocation from a reviewed newer local package atomically refreshes its
retained `Ark.Update.psm1` from the trusted local module already executing, before
publishing a pending runtime. Old/equal runtimes cannot replace the helper or a
newer pending candidate. If replacement fails, the previous helper and exact
current/pending state remain intact. Downloaded manifests and
`Install-ArkStagedPackage` never choose bootstrap code. Version directories remain
available for rollback and are not automatically pruned.

Retained MSVC-only helpers require `TKernel.dll` and reject a complete LLVM-MinGW
`libTK*.dll` package before staging. Their old online staging path cannot install
this migration. Launching the reviewed new package's `ARK.cmd` locally imports its
new module and prepares the LLVM runtime for the retained launcher. The refreshed
helper still launches and rolls back to a previous MSVC runtime. A fully automatic
transition needs a separately qualified MSVC-compatible bridge release. No such
online bridge or older-to-newer hosted delivery trial is claimed.

## Preparing release assets

`release.json` is the source for the sequence, roadmap phase, public iteration
code and numeric runtime. CMake validates it and generates the C++ header, Windows
version resource and `app-version.json`. Advance it for each new distributable;
the stable channel accepts numeric `major.minor.patch` versions and never
automatically installs prereleases, equal versions or downgrades.

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/build.ps1 -Test
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/package-release.ps1
```

The package script includes Studio, CLI, MCP, the native OCCT worker, DLLs,
UI assets, update scripts, version metadata and the Visual C++ x64 app-local
runtime from the installed build toolchain. `-MsvcRuntimeDirectory` can name that
toolchain runtime explicitly. It does not package repository drawings or tests.
LLVM-MinGW builds instead require `-CompilerDirectory` for matching compiler and
runtime redistribution notices. The archive retains its actual LLVM/MinGW DLLs.
The packager generates a CycloneDX SBOM, source/payload inventories and an unsigned
archive-bound provenance sidecar; see [trust foundation](TRUST_FOUNDATION.md).
Normal release packages must match `release.json`. Personal test packaging with
`-SkipUpdateManifest` preserves the compiled runtime identity and records the
packaging source identity separately; this does not attest its original compiler
inputs or advertise an update feed.
Update scripts are taken from the reviewed repository source so that a retained
compiled runtime cannot package the retired channel scripts. The archive includes
Apache-2.0/NOTICE and third-party notices; its README.txt links to the maintained
online handbook and connection guide. Review redistribution notices before publication.
The **Prepare Windows and Linux release packages** workflow builds/tests, audits
the exact generated SBOMs and uploads artifacts. Its existing explicit `publish`
option can publish an immutable prerelease only after both platform jobs pass;
publication remains disabled by default. The optional installer keeps its separate
build and relocated-installation checks.

These two reviewed assets belong on a release in `CenturyMesh/Ark` whose tag
matches the numeric runtime, for example `v0.9.17`. Mark evaluation releases as
prereleases; only a published stable release is discoverable through the updater's
`/releases/latest` endpoint. The future website can use the same package schema:

- `ark-windows-x64-0.9.17.zip`
- `ark-update.json`

The manifest format is:

```json
{
  "schema": 1,
  "product": "ARK",
  "version": "0.9.17",
  "platform": "windows-x64",
  "package": {
    "name": "ark-windows-x64-0.9.17.zip",
    "url": "https://github.com/CenturyMesh/Ark/releases/download/v0.9.17/ark-windows-x64-0.9.17.zip",
    "size": 123456,
    "sha256": "<64 lowercase hexadecimal characters generated from the archive>"
  }
}
```

The release API, repository and asset names are fixed in code. HTTPS requests
reject other repositories, plain HTTP, credentials and external redirects; only
GitHub release asset storage hosts are accepted after a trusted GitHub redirect.
The API and manifest have 1 MiB and 64 KiB limits. Packages are capped at 512 MiB,
4096 entries and 2 GiB expanded size; each transfer has bounded timeouts.
Extraction rejects path traversal, drive/UNC paths, alternate streams, Windows
reserved names, collisions and links. SHA-256 is checked before extraction and
local file receipts are rechecked before activation. A manifest cannot choose an
executable, installation path, command, script argument or alternate feed.

The trust authority is the GitHub repository owner and HTTPS. A checksum detects
corruption; it is not an independent publisher signature. Authenticode signing and
an independently pinned signing key are future release hardening work.

## Offline verification

```powershell
powershell -NoProfile -NonInteractive -ExecutionPolicy Bypass -File tests/updates_tests.ps1
ctest --test-dir out/native-release -C Release -R ota-updates --output-on-failure
ctest --test-dir out/native-release -C Release -R ota-launch --output-on-failure
```

These checks use temporary fake runtimes and ZIP files: numeric version ordering,
untrusted URLs, hashes, extraction paths/collisions, missing CAD binaries, unchanged
drawings, concurrent staging, activation, startup rollback, manual rollback,
interrupted pointer writes and corruption recovery. They do not contact GitHub or
launch an application. The real online delivery path still requires a configured
trusted production host and compatible package, followed by an end-to-end trial
from an older build.

`updates_launch_tests.ps1` additionally launches a temporary Windows GUI fixture:
startup acknowledgement, real process failure and fallback, exact Unicode/quoted/
empty arguments, working directories and environment restoration are checked
without a network request or changes to the user's installation.
It also retains the previous MSVC-only validator in an installed helper fixture,
observes rejection of LLVM staging, and checks local atomic helper migration,
locked-helper failure with unchanged pointers, LLVM activation, MSVC rollback and
rejection of helper downgrades.

Implementation follows the official
[GitHub releases API](https://docs.github.com/en/rest/releases/releases#get-the-latest-release)
and [Windows atomic file replacement](https://learn.microsoft.com/en-us/dotnet/api/system.io.file.replace).
