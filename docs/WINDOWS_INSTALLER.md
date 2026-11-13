# ARK Windows installer

The Windows setup (`installer/ArkSetup`, WPF on .NET 10) opens on the MeshSphere
key art with the ARK wordmark: **Welcome → Options → Installing → Done**, with an
Error page that shows the failing step and a link to the log. Options choose the
folder and the desktop and Start menu shortcuts. The normal per-user location,
`%LOCALAPPDATA%\ARK\app`, needs no administrator prompt. Reinstalling or
upgrading offers the previous folder, including the one an older Inno Setup
installer recorded. The interface follows the Windows language (English or
Portuguese); `--lang en|pt` forces one.

The key art has two layers: a still plate and the MeshSphere sphere, which wanders
slowly over it (sums of incommensurate sines for position, a ±3 % breathing scale and
a ±4° turn). While ARK installs it moves about 2.6 times faster with 1.3 times the
range, easing in and out over about a second. It stays in the right half of the
window, is driven by WPF's animation clock at 30 frames per second, and stays still
when Windows animations are turned off. At rest the two layers reproduce the
rendered frame; see [third-party notices](THIRD_PARTY_NOTICES.md) for its provenance.

The setup does not lay out ARK itself. It checks the package against the SHA-256
stamped into it at build time (the check and the extraction read the setup
through one handle that it holds open without write sharing), extracts it to its
own temporary folder
(`%TEMP%\ark-setup-<id>`, removed afterwards; folders left by a killed run are
removed by the next run) and runs that package's own `Install-ARK.ps1` (the same
`Initialize-ArkInstall` used by the portable ZIP), which creates `versions`,
`launcher`, `ARK.cmd`, `current.json` and `update.lock`. A newer setup therefore
preserves the selected runtime and prepares the newer one for the next launch;
the launcher keeps startup acknowledgement and rollback ([updates](UPDATES.md)).
The setup only installs and uninstalls: ARK updates itself in the app.

Before writing anything Setup reads `current.json`. **Reinstall** (the folder
already has this version) verifies `versions\<version>` against its receipt and,
when it is damaged, moves it aside and copies it again from the package, under
`update.lock`; the selection is kept. When the launcher selects this version but
its folder is missing, Reinstall copies it again. The damaged copy is deleted only
after the new copy has passed verification. If the repair fails or is stopped
part-way, Setup moves the damaged copy back when `versions\<version>` is missing,
and otherwise leaves it as `stage-<id>` (uninstall removes it). A folder whose
launcher selects or has
prepared a **newer** version is refused (exit code 8) unless `--allow-downgrade`
is given, which installs this version and selects it, keeping the version the
launcher would have started (the prepared one if any, else the current one) as
`previous` for ARK's own rollback. An activation that is still in progress is
refused (exit code 3) until ARK has been opened once. The disk check covers the
folder's drive and the `%TEMP%` drive, and both needs at once when they are the
same drive. It counts a full runtime copy on the folder's drive even when this
version is already there, because a repair keeps the damaged copy until the new
one is verified.

Setup adds `uninstall.exe`, `uninstall-notices.txt`, `ark-setup.json`, the
shortcuts (PowerShell running `launcher\ark-launch.ps1`, working folder
Documents) and the per-user entry `MeshSphere.ARK` in Installed apps. Its icon is
`uninstall.exe,0` and its version is the one `current.json` selects; the launcher
keeps that version current after in-app activations and rollbacks. The entry is
written before an Inno Setup entry (`MeshSphere.ARK_is1`) that owns the same
folder is removed with Inno's `unins000` files, so Windows lists one ARK and a
failed write never leaves none. A folder typed in the folder box or passed with
`--dir` is used as given (only the folder picker adds `ARK`), so Setup refuses a
non-empty folder unless it is an ARK install (`current.json` or `ark-setup.json`,
or the folder the Inno Setup entry names). Uninstall first takes `update.lock`
exclusively, as `Enter-ArkUpdateLock` does, and fails at once while a launch or an
update holds it or an ARK program runs from the folder; it then removes the
runtime layout, those additions and leftovers of an interrupted update; the folder
goes only when it is then empty. Drawings, settings and everything else under
`%LOCALAPPDATA%\ARK` and Documents stay. Keep drawings outside the immutable
`versions` directory.

Command line: `--silent [--dir X] [--no-desktop] [--no-startmenu]
[--allow-downgrade]` installs without UI; `uninstall.exe --uninstall [--quiet]`
uninstalls. `--name=value` and `/name` forms are accepted. Scripts written for the
old Inno Setup installer keep working: `/SILENT` and `/VERYSILENT` install without
UI (run on `uninstall.exe` they uninstall quietly), `/DIR=X`, `/NOICONS` (no Start
menu shortcut), `/LANG=english|portuguese`, `/LOG` and `/LOG=FILE` (a copy of the
log), `/SUPPRESSMSGBOXES`, `/CURRENTUSER`, and `/NORESTART`, `/SP-`, `/NOCANCEL`
and the `/...APPLICATIONS` switches, which need nothing (Setup never restarts
Windows and refuses rather than closes a running ARK). `/ALLUSERS` is refused: ARK
installs per user. With any silent or message-box switch Setup never shows a
dialog; errors go to the log and the exit code. `--dir` needs a full path without
quotes or wildcards; `"C:\Apps\"` (a backslash right before the closing quote
makes Windows join the following arguments into the folder) is refused with that
explanation. Exit codes: 0 ok, 1 failure, 2 arguments or folder refused, 3 ARK is
running from that folder or holds `update.lock`, 4 no package in the setup or the
package failed its checksum, 5 not an ARK folder, 6 another setup is running,
7 `Install-ARK.ps1` failed, 8 a newer version is installed, 9 not enough disk
space, 10 cancelled. The log is `%TEMP%\ARK-Setup.log`.

The installer respects the Windows animation setting (page transitions, the done
badge, the window fade and the sphere), names its icon buttons, folder box and
progress for screen readers, closes with Esc when nothing is running, and scales
to the work area of the monitor it opens on.

## Build and verify

Build a reviewed Windows ZIP first, then, with the .NET 10 SDK:

```powershell
powershell -NoProfile -File scripts/build-installer.ps1 `
  -PackagePath out/releases/ark-windows-x64-<version>.zip
```

`dotnet publish` produces a self-contained single-file `ARK-Setup.exe` with the
package's SHA-256 stamped into its assembly metadata (`-p:ArkPayloadSha256`); the
script appends the unchanged ZIP and a 40-byte footer and writes
`ARK-<code>-windows-x64-setup.exe`. The footer is `ARKPAYL2`, payload offset and
length (int64), then the stub's PE CheckSum, certificate table offset and size
(uint32) and a zero uint32, little endian (`installer/ArkSetup/Core/Payload.cs`,
`scripts/Ark.SetupPayload.psm1`). The package is validated with
`Expand-ArkPackage` first and read back from the finished setup the way Setup
reads it. Source and payload identity must agree, and the code and version shown
come from `release.json` (or the runtime identity with `-UseRuntimeIdentity`).
Existing installer filenames are never overwritten. A public setup is a
distributable artifact: the script refuses it when the source tree has
uncommitted changes or the package's release evidence records a dirty tree;
`-AllowDirtyTree` builds one anyway for a local test that is not distributed.

### Signing

Authenticode appends a certificate table to the end of the file and points the PE
security directory at it, so Setup looks for the footer right before that table
(after up to 7 zero bytes of alignment) when the directory is set, and refuses
data after it. Sign in this order, which `-SignScript <script>` performs (the
script is called as `& <script> -Path <file>` and signs that file in place):

1. Sign the published stub. It is the uninstaller: Setup writes exactly these
   bytes as `uninstall.exe`, so the uninstaller carries its own signature instead
   of being an unsigned copy.
2. Append the package and the footer, which records the stub's CheckSum and
   certificate table. The setup's security directory is cleared, so the stub's
   table is ordinary data inside the setup.
3. Sign the whole setup. Its signature covers the stub, the package and the
   footer. When Setup writes `uninstall.exe` it restores the stub's CheckSum and
   security directory, which makes the file identical to the signed stub.

The script checks both signatures (a public setup needs a trusted certificate;
both must use the same one), that the footer is found, that the package digest is
unchanged and that the `uninstall.exe` Setup would write hashes like the signed
stub. A typical script:

```powershell
param([string]$Path)
& signtool.exe sign /fd SHA256 /tr <timestamp-url> /td SHA256 /sha1 <thumbprint> $Path
if ($LASTEXITCODE -ne 0) { throw "signtool failed ($LASTEXITCODE)" }
```

No ARK release is signed yet; trust and SmartScreen reputation need a real
code-signing certificate and remain unverified.

### Size

Measured with the 0.9.47 package (103,472,945 bytes): the setup was 166,056,592
bytes with the earlier code and is 166,069,459 bytes now; the stub (the
compressed single-file `ARK-Setup.exe`, also the size of `uninstall.exe`) grew
from 62,583,623 to 62,596,474 bytes. `EnableCompressionInSingleFile` was already
on; WPF does not support trimming, and a framework-dependent or non-WPF
uninstaller would need another runtime or user interface, so `uninstall.exe`
stays the stub. The reviewed ZIP is appended unchanged, never recompressed. The
.NET single-file host extracts five WPF native libraries (8,215,952 bytes) to
`%TEMP%\.net\<exe name>\<hash>` on every start and left them there. Each Setup or
uninstall run now removes the copies that other ARK setup builds left (unless a
setup of that name is running), and `uninstall.exe` removes its own copy once it
has deleted itself. A run does not remove its own copy while it exits: in a test
that renamed it right after `Main` returned, the process sometimes crashed in
WPF's C++/CLI module unloader (`DllNotFoundException` for
`vcruntime140_cor3.dll`, exit code 0xE0434352). So the copy of the last Setup
run stays until the next Setup or uninstall run.

For private tests, add `-TestIsolationId <unique-name>` and a separate output
directory. Such builds have their own installed-app registration
(`MeshSphere.ARK.Test.<id>`), a `-test-` filename suffix, and no shortcuts. Any
setup accepts `--test-isolated <id>` for the same isolation; `test-distribution.ps1`
uses it to exercise the public setup. Add `-SelfTest` only to a private build to
include the page renderer (`--screenshot <dir> [--screenshot-scale N]
[--screenshot-times T1,T2,…] [--layout-only]`), which draws every page to PNG and
writes a layout report without installing. The times also capture Welcome (calm) and
Installing (lively) with the sphere posed at those phases; the report checks the
sphere's whole wandering range and that the live animation moves and eases.
Public builds omit that code. Exercise the real payload with:

```powershell
powershell -NoProfile -File tests/installer_smoke_tests.ps1 `
  -InstallerPath <private-test-setup.exe> -OutputDirectory out/installer-smoke `
  -CaptureWizard
```

The harness checks a remembered path containing spaces (seeded through a
synthetic isolated Inno Setup entry, which must be replaced), runtime and licence
payload, the .NET notices, repeat install, uninstall (waiting for `uninstall.exe`
to remove itself), and preserved synthetic drawings and settings plus the real
launcher pointer, shortcuts and registrations.
Supply a lower-version `-PreviousInstallerPath` built with the same isolated ID
(this setup or an older Inno one) to exercise upgrade, activation and manual
rollback. This direct state check does not claim a real startup acknowledgement
or hosted delivery trial. `-CaptureWizard` needs a `-SelfTest` build; it fails on
controls outside their page, overlapping blocks, clipped text, or a sphere that could
reach the text panel. Inspect the
PNGs before declaring visual acceptance; `-LayoutOnly` skips them.

The install engine itself is covered without a native build by:

```powershell
powershell -NoProfile -File tests/installer_engine_tests.ps1 [-Signing]
```

It builds isolated setups around a synthetic package (placeholder runtime files
with the repository's `Install-ARK.ps1` and update module) and checks the Inno
switches and `/LOG`, refusals without dialogs (unknown and relative arguments, the
trailing-backslash case, `/ALLUSERS`, a public setup given `--screenshot`), a
non-empty foreign folder, reinstall repair of a damaged or missing runtime, a
repair that dies or fails part-way (a package whose update module is made to fail
on purpose: the damaged copy is moved back or kept, never deleted), downgrade
refusal and `--allow-downgrade` (also over a prepared newer version), the
Installed apps version after activation, failed activation and rollback, an
interrupted activation, install and uninstall under a
held `update.lock`, `uninstall.exe /VERYSILENT`, self-removal from a folder named
with `%`, `!` and `&`, a changed package byte, a synthetic certificate table and
data after it, leftover temporary folders and native library caches. `-Signing`
also signs a setup and its uninstaller with a temporary self-signed certificate in
`CurrentUser\My` (no trust store is changed; it is removed afterwards) and checks
both signatures. The pull-request workflow has a `windows-installer` job that
builds the public and self-test configurations, renders the pages (layout only)
and runs these checks on a hosted Windows runner. That job has not yet been
observed on a hosted runner; only local runs of the same commands are verified.
`scripts/test-distribution.ps1` also checks that the public setup refuses
`--screenshot`. `dotnet run --project tests/installer_process_070a` checks, besides
running-process detection, that an opened payload keeps reading the same file
after the setup is renamed and another file is put at its path, that no other
handle can write to it meanwhile, and that the disk plan counts a runtime copy
when the version folder exists.

This addresses **F.03/F.05 — Parcial**. Local installer tests do not establish
clean-machine setup, Authenticode publisher trust, production delivery, or
measured first-valid-part and adoption outcomes.
