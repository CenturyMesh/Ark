# ARK Windows installer

The Windows setup opens with the existing ARK mark centred on a dark welcome
page. Choose an installation folder and select **Install**. Setup prepares the
runtime, retained launcher, desktop and Start menu shortcuts, and uninstaller.
The normal per-user location requires no administrator prompt. Reinstalling or
upgrading offers the previously chosen folder.

The installer uses the same versioned runtimes and atomic selection as
[ARK updates](UPDATES.md). A newer setup preserves the selected runtime and
prepares the newer runtime for the next launch; the launcher retains startup
acknowledgement and rollback. Drawings and application settings are not
uninstalled. Keep drawings outside the immutable `versions` directory.

## Build and verify

Build a reviewed Windows ZIP first, then use Inno Setup 6.3 or newer:

```powershell
powershell -NoProfile -File scripts/build-installer.ps1 `
  -PackagePath out/releases/ark-windows-x64-<version>.zip `
  -InnoCompiler 'C:/tools/Inno Setup 6/ISCC.exe'
```

Source and payload identity must agree. Existing installer filenames are never
overwritten. Artwork is derived from the existing transparent application mark;
the original asset and all dependency notices remain unchanged.

For private tests, add `-TestIsolationId <unique-name>` and a separate output
directory. Such builds have their own installed-app registration, a `-test-`
filename suffix, and no desktop or Start menu shortcuts. Add `-SelfTest` only to
a private build to enable the wizard's own control/capture harness. Public
builds omit that code. Exercise the real payload with:

```powershell
powershell -NoProfile -File tests/installer_smoke_tests.ps1 `
  -InstallerPath <private-test-setup.exe> -OutputDirectory out/installer-smoke `
  -CaptureWizard
```

The harness checks a path containing spaces, remembered folder, runtime and
licence payload, repeat install, uninstall and preserved synthetic drawings,
settings, live selection pointer and desktop shortcut. Supply a lower-version
`-PreviousInstallerPath` built with the same isolated ID to exercise upgrade,
activation and manual rollback. This direct state check does not claim a real
startup acknowledgement or hosted delivery trial. Inspect the generated page
captures before declaring visual acceptance; unsupported/blank captures do not
establish it.

This addresses **F.03/F.05 — Parcial**. Local installer tests do not establish
clean-machine setup, Authenticode publisher trust, production delivery, or
measured first-valid-part and adoption outcomes.
