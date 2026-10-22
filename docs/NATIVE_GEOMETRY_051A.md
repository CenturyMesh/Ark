# Native geometry verification, 051A

A.03/B.03: the 050B diagnostic run selected an unpatched OCCT 8.0.1 SDK
with `ARK_ALLOW_UNPATCHED_OCCT=ON`. Two existing regression tests failed:
`native-solid` at shallow C0 join classification, and `native-surface` at the
translated analytic C2 frustum (relative volume error 4.26611e-6 against 1e-8).
The other 151 tests in that diagnostic run passed. This is evidence about that
SDK selection, not a reason to weaken either geometric oracle.

The repository already owns the exact source fixes in `ark-offset-001`:

- `0005-native-geometry-custody.patch` makes `BRepOffset_Analyse` honor encoded
  regularity instead of the fillet helper's weak visual tangent condition. The
  existing fixture sews a plane to a planar B-spline at negative, zero and
  positive angles and requires genuine shallow concave/convex C0 joins to remain
  classified as such.
- `0002-variational-factor-qr.patch` solves the bounded C2 independent-dimension,
  two-passpoint domain from its original factors, retaining constraint assembly
  and explicit rank checks. This avoids the unstable normal-equation solve in
  the tested loft domain. The existing analytic-frustum fixture independently
  integrates volume across scales and translation; the unchanged relative
  threshold is 1e-8. Other solver domains retain their original dispatch.

The missing build route was Windows LLVM-MinGW: the PowerShell dependency builder
only prepared MSVC, while the local compiler and old SDK were MinGW. The builder
now accepts `-Toolchain LLVMMinGW -CompilerDirectory <bin>`, builds into a separate
patchset-qualified directory, and preserves pinned archive, exact target hashes,
license notices and the installed shared-library inventory. No shared old SDK is
rewritten, no patch is redefined, and no fixture or geometry tolerance is relaxed.

The optional retained-metadata codec also now exposes its Windows declarations
locally and uses the documented const-input function-pointer ABI for dynamically
loaded compression. The minimum Windows target and unavailable-codec behavior
remain unchanged. This removes the external MinGW forced-include workaround.
The geometry-storage regression passed with the changed source compiled at
`_WIN32_WINNT=0x0601`, without that workaround.

Build provenance: OCCT V8_0_1 archive SHA-256
`7c033d917ee8f040c0512d289dcc5f02c148889d5bac17c3e25639accb44f0da`;
patchset manifest SHA-256
`2fbbd4f68b8c06a36aa04787d378540fa6e9ee43d353a57d7e09870c185f9562`.
LLVM-MinGW 20261006 UCRT x86-64; Release; four compiler jobs. The SDK's
`.ark-occt-build.json` records all installed OCCT DLL bytes and the
`clean-source-rebuild` route. It is an observed build declaration, not independent
compiler-input attestation or manufacturing qualification.

Observed 051A local result: **Verificado no escopo** for these two existing
regression suites and SDK selection. The same executable bytes were run first
against the original SDK (both exit 1), then against the clean patched SDK (both
exit 0). `native-solid` passed all its cases. `native-surface` passed all its
cases, including the following analytic C2 frustum relative-volume errors:

| Scale | Translation | Unpatched | Patched | Existing limit |
| --- | --- | --- | --- | --- |
| 1 | 0 | 2.5307e-11 | 7.39408e-15 | 1e-8 |
| 30 | 0 | 1.18897e-9 | 3.29035e-14 | 1e-8 |
| 30 | 100000 | 4.26611e-6 | 6.8587e-13 | 1e-8 |

Controlled executable SHA-256 values:
`native-solid`: `1a6425e6d058a6fd4a31934b9fe1013898ad3255b79059383c3e943423d3412b`;
`native-surface`: `79e880024837ba940d95d40758b03d056d65c9490ed42bd9e2c055ac5d2e8b08`.
The installed SDK inventory was verified by `ApplyOcctPatch.cmake VERIFY_INSTALLED`.
A separate fresh ARK Release configuration accepted that SDK with
`ARK_ALLOW_UNPATCHED_OCCT=OFF` and empty C/C++ extra flags, without the external
compatibility include. The dependency builder disables CMake's global package
registry export; SDK choice remains explicit and does not require registry writes.

This comparison isolates the dependency repair using existing test binaries; it
does not stand in for an integrated 051A application build, complete-suite run,
packaging or deployment. Those are separate delivery gates. No manufacturing or
arbitrary-surface qualification is inferred from these regression fixtures.

## A.04/B.03 cancellation across evaluation threads

The request cancellation scope and timeout allowance were thread-local. Native
jobs dispatched to the feature pool therefore lost their owner's cancellation
policy and could remain in a worker RPC until the default watchdog expired.
Each submitted job now owns a copy of that policy and installs an RAII scope on
the pool thread. Scope exit restores the thread's prior policy, including after
native failure. This changes neither geometric authority nor tolerances.

The native variant of `ark-evaluation-schedule-032d-tests` exercises four independent
native boxes on two feature workers with the result cache disabled. Its cancellation
callback permits dispatch and then cancels inside the real isolated process wait.
The local isolated test started two native processes and returned in 29.27 ms
against a 120-second allowance; every job reported `NATIVE_CANCELLED`. A subsequent
recompute succeeded on reused pool threads without invoking the prior callback.
The test also verifies inheritance and restoration of the request allowance.
This evidence uses freshly compiled changed objects linked with the existing
baseline library and worker. An integrated build and suite remain separate gates.

The integrated 051A build subsequently passed `native-solid`, `native-surface`,
`evaluation_schedule-032d`, `async-exact-046b` and `async-authoring-051a` against
the verified SDK. The fresh native CMake configuration has
`ARK_ALLOW_UNPATCHED_OCCT=OFF` and empty C/C++ extra flags. Full-suite outcomes,
including separately repeated filesystem failures, are recorded in
[051A](iterations/051A.md). This is local build evidence, not hosted CI,
distribution or manufacturing qualification.
