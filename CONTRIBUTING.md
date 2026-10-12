# Contributing to ARK

ARK welcomes focused improvements to editable CAD, dependable documents and useful
agent workflows. Start with the [README](README.md), [handbook](docs/HANDBOOK.md)
and repository [development rules](AGENTS.md). Agent integrations must also follow
the [CAD protocol](docs/AGENTS.md).

ARK source is licensed under [Apache-2.0](LICENSE). Unless you explicitly state
otherwise, contributions submitted for inclusion are under the same license,
consistent with its contribution terms. Third-party components retain their own
licenses. Submit only work you have the right to contribute; do not disclose
private code or customer designs without authorization.

## Choose a useful change

Bug reports should include a minimal, shareable reproduction, the runtime version
and iteration code, platform, expected behavior and actual result. Geometry reports
need the input and command sequence that produced the rejection or incorrect result.
Screenshots help explain interaction; they cannot establish exact geometry.

For a new capability or an architectural change, open an issue first with the
user problem, proposed scope, relevant [roadmap IDs](docs/ROADMAP.md), acceptance
criteria and remaining questions. Small fixes and documentation corrections can go
straight to a pull request. An issue records discussion; it is not a delivery promise.

Do not attach private `.ark` documents, provider tokens, credentials or proprietary
STEP files. Make a synthetic reproduction or obtain explicit sharing authorization.
Report vulnerabilities according to the [security policy](SECURITY.md).

## Set up a checkout

Use a fork when contributing from outside the maintainer team; maintainers can
create a topic branch in the repository. For example:

```sh
git clone https://github.com/YOUR-ACCOUNT/Ark.git
cd Ark
git remote add upstream https://github.com/CenturyMesh/Ark.git
git fetch upstream
git switch -c fix/short-description upstream/main
```

For a repository checkout with write access:

```sh
git switch main
git pull --ff-only
git switch -c feature/short-description
```

Use a descriptive branch such as `fix/stale-mate-reference`,
`feature/sketch-diagnostics` or `docs/first-assembly`. Keep each pull request focused
on one coherent behavior. Do not commit directly to `main` for routine contributions.

Build on Windows with the native toolchain:

```powershell
.\scripts\build.ps1 -Test
```

This uses Visual Studio 2022 x64, CMake and the pinned OCCT SDK. See
[native setup](docs/NATIVE_BUILD.md) for prerequisites. Contributors working on the
portable core can use:

```sh
cmake -S . -B out/core -DARK_WITH_OCCT=OFF -DCMAKE_BUILD_TYPE=Release
cmake --build out/core --config Release --parallel 2
ctest --test-dir out/core -C Release --output-on-failure
```

Studio and native OCCT changes require the corresponding Windows checks; a core-only
build does not cover those paths.

## Implement within the contracts

- Keep geometric authority in native B-reps, explicit inputs and producer contracts.
  Do not make a screenshot, reference DXF or display mesh select exact geometry.
- Preserve `expected_revision`, atomic publication and explicit rejection. Invalid
  references or failed geometry must not publish a partially edited document.
- Discover persistent semantic references; do not store tessellation or B-rep
  enumeration indexes as stable identities.
- Keep Focused as the default agent mode and Parallel optional. Do not introduce
  agent turn/token budgets without a new user instruction.
- Treat ARK, STAGE, Trace and MeshWorks as separate product owners. An integration
  needs an explicit contract and evidence before it is described as available.
- Keep user sessions, local designs, secrets, generated binaries and dependency
  build caches out of commits. Only add examples explicitly cleared for distribution.
- Respect third-party notices and review redistributed content. The commercial
  strategy is not an adopted code license.

Follow the surrounding C++ conventions and keep changes in the module that owns
the behavior. Avoid broad formatting changes in a functional pull request.

## Verify the behavior

Choose checks that exercise the actual risk. Geometry needs native positive and
negative cases; revision contracts need conflict and atomicity checks; persistence
needs round trips and recovery cases; updates need their integrity/launch tests.
UI changes need captures and inspection of the affected controls at supported
window sizes, in addition to appropriate self-tests.

Run the appropriate build/test suite after the change. A focused CTest filter is
useful while iterating, for example:

```powershell
ctest --test-dir out/native-release -C Release -R release-metadata --output-on-failure
git diff --check
```

Run the broader native or core suite applicable to the final scope before review.
State exactly which commands ran and which did not. Do not report hosted CI as
passed unless the job actually ran successfully. Live Studio tests require an
OpenGL-capable desktop; [validation](docs/VALIDATION.md) explains the distinction.

Update contracts and affected roadmap statuses when behavior changes. Milestone
status values are `Planeado`, `Em curso`, `Parcial`, `Verificado no escopo` and
`Bloqueado`; the English roadmap explains their meaning. Reference stable IDs such
as `B.02` and `D.04`, and record evidence and limits, including failed attempts.

## Open a pull request

```sh
git add <changed-files>
git commit -m "Fix stale semantic-reference rejection"
git push -u origin fix/short-description
```

Open a pull request with `main` as its base. Describe the concrete problem and the
resulting behavior, explain relevant design choices, link the issue and roadmap
IDs, and include the actual validation results. Add before/after captures for UI
changes. Disclose unsupported cases or checks that remain unrun.

Respond to review on the same branch and keep the pull request description aligned
with the final implementation. Use merge or rebase deliberately to update a topic
branch; coordinate before rewriting a shared branch and never force-push `main`.
Maintainers review and merge contributions. The templates help organize review;
they do not imply that branch protection is already configured on the remote.

## Release identity and document compatibility

Ordinary fixes within an iteration do not create a release code per file or per
pull request. The maintainer preparing a new distributable updates `release.json`:
globally increasing sequence, principal A–G phase, unique code and numeric runtime
version, with addressed milestones and evidence. CMake generates downstream
identity; do not hard-code separate copies in the app, MCP or package metadata.

The `.ark` document version changes only when its persisted contract requires it.
Changing the runtime identity alone is not a document migration. Preserve legacy
read behavior and explicit unsupported-version rejection.
