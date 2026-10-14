# Windows distribution

Milestones A.05/F.03/F.05: Parcial. ARK source is Apache-2.0; dependency notices remain required. Published preview packages and their exact verification belong to [Releases](https://github.com/CenturyMesh/Ark/releases). Native/UI, installer, clean-machine and manufacturing acceptance are separate scopes.

Download the complete Windows ZIP, extract it and run ARK.cmd. Install-ARK.cmd, when included, prepares the per-user launcher and desktop shortcut. A setup EXE is optional and should be used only when the release supplies it with installer-test evidence. Windows 10/11 x64 and an OpenGL 3.3 driver are required; packages retain runtime dependencies and notices. Modeling needs no AI account. Each agent connector keeps its own authentication requirements.

The application uses a versioned launcher under %LOCALAPPDATA%\ARK\app. User documents and settings stay outside immutable application versions. ARK's update staging/activation/rollback handles subsequent updates without another installer. The trusted online update transport remains pending.

Build and verify a current package:

```powershell
./scripts/build.ps1 -Test -Jobs 2
./scripts/package-release.ps1
```

release.json determines source identity; packaged binaries retain their generated runtime metadata. Existing archives/releases are not overwritten. No user drawings or private settings are included.

To create an optional installer, install Inno Setup 6.3+ and use the archive matching the current source:

```powershell
./scripts/build-installer.ps1 -PackagePath out/releases/ark-windows-x64-VERSION.zip
./scripts/test-distribution.ps1 -PackagePath out/releases/ark-windows-x64-VERSION.zip -InstallerPath out/releases/ARK-CODE-windows-x64-setup.exe
```

Replace VERSION and CODE with the generated identity, not a reused code. The isolated smoke verifies package geometry, STEP, installation receipts and uninstall. tests/distribution_tests.ps1 covers packaging fixtures; fixture success alone does not qualify a real installer.

Actions -> Prepare Windows and Linux release packages -> Run workflow builds and tests both platforms. The installer input additionally builds/tests the Windows setup. Publication is explicit, defaults to false and runs only after both platform jobs succeed. It attaches checksums and pinned OCCT source, and refuses an existing version tag. Hosted native results do not establish interactive Studio acceptance on another computer.

For an explicit personal build from existing executables:

```powershell
./scripts/publish-test-build.ps1 -Publish
```

This path packages existing executables without rebuilding or running tests. Omit -Publish to prepare locally; add -Installer only when Inno Setup is available. GitHub CLI must already be authenticated. The compiled identity is preserved and an existing personal release is never replaced. Its tag identifies the packaging checkout, not independently attested binary source provenance. Notes label the untested personal scope; this path does not replace verified preview/release qualification or publish an OTA feed.