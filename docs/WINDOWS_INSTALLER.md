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

The setup does not lay out ARK itself. It extracts the packaged runtime to a
temporary folder and runs that package's own `Install-ARK.ps1` (the same
`Initialize-ArkInstall` used by the portable ZIP), which creates `versions`,
`launcher`, `ARK.cmd`, `current.json` and `update.lock`. A newer setup therefore
preserves the selected runtime and prepares the newer one for the next launch;
the launcher keeps startup acknowledgement and rollback ([updates](UPDATES.md)).
The setup only installs and uninstalls: ARK updates itself in the app.

Setup adds `uninstall.exe`, `uninstall-notices.txt`, `ark-setup.json`, the
shortcuts (PowerShell running `launcher\ark-launch.ps1`, working folder
Documents) and the per-user entry `MeshSphere.ARK` in Installed apps. When an
Inno Setup entry (`MeshSphere.ARK_is1`) owns the same folder, Setup removes it
and Inno's `unins000` files, so Windows lists one ARK. Uninstall removes the
runtime layout, those additions and leftovers of an interrupted update; the
folder goes only when it is then empty. Drawings, settings and everything else
under `%LOCALAPPDATA%\ARK` and Documents stay. Keep drawings outside the
immutable `versions` directory.

Command line: `--silent [--dir X] [--no-desktop] [--no-startmenu]` installs
without UI; `uninstall.exe --uninstall [--quiet]` uninstalls. Exit codes: 0 ok,
1 failure, 2 arguments or folder refused, 3 ARK is running from that folder,
4 no package in the setup, 5 not an ARK folder, 6 another setup is running,
7 `Install-ARK.ps1` failed, 10 cancelled. The log is `%TEMP%\ARK-Setup.log`.

## Build and verify

Build a reviewed Windows ZIP first, then, with the .NET 10 SDK:

```powershell
powershell -NoProfile -File scripts/build-installer.ps1 `
  -PackagePath out/releases/ark-windows-x64-<version>.zip
```

`dotnet publish` produces a self-contained single-file `ARK-Setup.exe`; the
script appends the unchanged ZIP and a 24-byte footer (`ARKPAYLD`, offset,
length) and writes `ARK-<code>-windows-x64-setup.exe`. The package is validated
with `Expand-ArkPackage` first. Source and payload identity must agree, and the
code and version shown come from `release.json` (or the runtime identity with
`-UseRuntimeIdentity`). Existing installer filenames are never overwritten.

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
payload, the .NET notices, repeat install, uninstall, and preserved synthetic
drawings and settings plus the real launcher pointer, shortcuts and registrations.
Supply a lower-version `-PreviousInstallerPath` built with the same isolated ID
(this setup or an older Inno one) to exercise upgrade, activation and manual
rollback. This direct state check does not claim a real startup acknowledgement
or hosted delivery trial. `-CaptureWizard` needs a `-SelfTest` build; it fails on
controls outside their page, overlapping blocks, clipped text, or a sphere that could
reach the text panel. Inspect the
PNGs before declaring visual acceptance; `-LayoutOnly` skips them.

This addresses **F.03/F.05 — Parcial**. Local installer tests do not establish
clean-machine setup, Authenticode publisher trust, production delivery, or
measured first-valid-part and adoption outcomes.
