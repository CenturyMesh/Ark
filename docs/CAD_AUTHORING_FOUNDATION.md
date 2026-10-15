# CAD authoring and recovery foundation in 029B

This iteration adds bounded native editors, feature-history controls and recovery
to the existing Studio authoring workspace. The current direct handles, embedded
image/stamp tools, design containers, technical-drawing viewport, visibility dock
and provider controls remain the corresponding product paths. These additions
address A.02, A.04, B.01 and B.02 within the domains described below; they do not
establish broad CAD parity or manufacturing qualification.

## Native editors and navigation — B.01

Native forms fit the current monitor work area and scale their fonts and controls
to the window's reported DPI. Large forms retain every control in a clipped child
viewport. Wheel, Page Up/Page Down, the scrollbar and keyboard focus reveal fields;
Apply and Cancel remain outside the scrolling region. Scrolling preserves typed
text, checkboxes and selections. Drawing-dimension column layouts and the wider
Ollama model dropdown are preserved, with a single-column fallback where needed.

The automated overflow fixture exercises 24 fields at a 1280×720 work-area override
and 96, 144 and 192 DPI layout values, including the last field and cancellation.
This verifies those layouts, rather than a physical multi-monitor DPI transition.
The main Studio process retains its existing Windows DPI-awareness mode.

## Retained feature history — B.02

Manage → Features and the feature-tree context menu open the retained feature list.
Select a feature to inspect its direct inputs, consumers and driving sketches.
Move up/down, suppression and an inclusive rollback tip use the ordinary
revision-checked document transaction. Rollback retains later source recipes and
each feature's suppression state; Resume all evaluates the full history. Invalid
dependency orders and suppression of required definitions reject without a
published change. Undo restores an accepted action.

Terminal output discovery considers active consumers, so a source at the rollback
tip remains visible when its later consuming feature is inactive. Existing
assembly visibility and selective-loading rules also govern displayed output.

Sketch diagnosis reports conflicting constraint IDs, named affected entities and
review actions. It keeps per-constraint residuals, local Jacobian rank and local
redundancy distinct. A diagnosis never removes constraints, changes their targets
or widens the convergence tolerance. Local rank and residual evidence do not prove
a unique global solution.

## Separate recovery candidates — A.02

Changed documents can be captured about every 30 seconds when Studio is idle.
Captures use separate integrity-checked recovery containers and a writer-specific
filename; ordinary source files remain explicit Save destinations. Candidate
discovery precedes capture for the opened source.

Recovery prompts offer opening a candidate unsaved, discarding that candidate or
retaining the remaining candidates and pausing capture for the current drawing.
Opening a candidate starts a separate capture lineage. Other writers' and unchosen
candidates remain retained. Save As chooses the recovered drawing's destination;
accepting recovery does not silently replace the original file. Capture errors are
shown in Studio status. This is a local recovery scope, not complete crash,
disk-full, update-interruption or multi-process qualification.

## Actual Studio interaction benchmark — A.04

`--self-test-interaction-performance-ui` creates synthetic fixtures containing
16, 128 and 512 analytic box definitions with translated occurrences. Each size
receives three warmups and 21 measured driver edits through Windows mouse-message
dispatch, the actual native parameter editor and native Apply control. Every
successful sample verifies one published revision, recomputation of all fixture
features and the displayed body count.

The report separates Apply-to-scene completion, the reported model recomputation
subset, `drawFrame` submission and `SwapBuffers` return. The normal Studio progress
worker and message pump remain involved. Automated dialog dwell is recorded
separately. Preparation draws and drains prior GL work outside the timed edit.
Raw warmups, measured attempts, failures, nearest-rank p95 values, fixture/runtime
hashes and CPU/Windows/OpenGL/render settings are retained in a unique output
directory beside fixture captures. Incomplete runs start with `valid:false`.

The hidden-window context requests swap interval zero. These timings do not
measure physical input-device latency, GPU completion, compositor presentation,
monitor scanout, OS-cold behavior or native OCCT workloads. They are observations,
not latency acceptance thresholds or evidence of improvement over another build.

## Verification commands

Configure Windows native tests with `ARK_TEST_STUDIO_LIVE=ON`. The relevant CTest
names are `sketch-diagnostics`, `studio-editor-scroll-ui`, `studio-history-ui`,
`studio-recovery-ui`, `studio-configuration-workspace-ui` and
`studio-interaction-performance-ui`. The last test has a 300-second limit; live UI
checks run serially. Standalone Studio flags accept `--shot OUTPUT.png --size 1280
800`. All generated fixtures are synthetic and stay under ignored build output.

Fresh 029B execution receipts and actual limitations are recorded below;
earlier iteration evidence is not substituted for this port.
See [configurations and loading](PRODUCT_CONFIGURATIONS_UI.md) and the existing
[drawing contract](DRAWINGS.md) for their distinct authoring workflows.

## Bounded native geometry corpus — B.03

The repository-authored [native fixture](../tests/corpus/native-authoring-v1.json)
uses canonical LF bytes pinned by the [manifest](../tests/corpus/manifest-v1.json):
SHA-256 `94add7d676685b6b054ce58ffa32e924869ff91b3c6e0fe1c2385e7f390e1dfe`.
Seed `19020` runs each family at scale `0.5`, `1` and `2`, retaining the fixed
`1e-6` mm geometry tolerance.

| Geometry family | Measured positive scope | Negative and explicit source repair |
| --- | --- | --- |
| Four-section loft | Eccentric stations, rotated circular seams, changing radii, valid solid, bounded volume and exact height | Collapsed stations reject; authored separation restores the loft |
| Two-guide loft | Ordered contacts, 33 native guide checkpoints and analytic cylindrical volume | A guide missing its sections rejects; authored contact restores it |
| Curved sweep | Quarter-torus sweep and analytic volume | Degenerate arc rejects; authored radius restores it |
| Varying sweep | Three different sections, straight-path analytic volume and curved-path native tangent frames | Unordered station fractions reject; authored ordering restores it |
| Warped patch | Closed nonplanar boundary, native G0 error and bounded area | Open boundary rejects; authored closing edge restores it |
| Cylinder trim/thicken | Exact periodic side split at 30% height, retained area and annular-wall volume | Zero thickness rejects; authored positive thickness restores it |
| Projected wire trim | Raised loop projected onto a warped surface, valid retained face and bounded area | Boundary keep point rejects; authored interior point restores selection |
| Missing-cap shell | Five explicit box faces form an open connected shell | Open shell cannot form a solid; an explicitly authored sixth face closes it with analytic volume |
| Sewn contact/crease | Native support gaps/angles and an intentional 90-degree crease | Finite gap rejects connected sewing; authored boundary contact restores it |

Fresh 029B checks in `out/geometrychecks` passed **84 transactions: 57 accepted
and 27 typed rejections**, plus **nine native plane-trim guards**. The accepted
transactions independently validate every exact B-rep, check declared geometric
measurements, serialize/read authoring and recompute with area/volume comparison.
Rejected transactions retain identical source JSON, journal, revision and
evaluated B-rep bytes. The generated `native-authoring-corpus-v1.receipt.json`
records fixture hashes, measured values and source-preservation hashes without
user drawings or B-rep payloads. The focused CTest receipt is
`geometry-targeted-attempt1.xml`; this run also passed `native-surface` and the
canonical `associative-drawings` suite. Other builds require their own receipts.

Periodic surface plane trimming uses one exact face/plane splitter and retains
both pieces until validation. Exactly one connected valid face must lie on each
side, and both areas must conserve the original area. Native support/curve bounds
in plane coordinates classify the signed-distance ranges with the unchanged
`1e-6` mm tolerance; meshes and centroid samples do not choose a side. The nine
guards accept both retained sides, inverted normals and an oblique cut, while
rejecting boundary, outside, tangent, coplanar and disconnected cases.

Fill reports its native G0 boundary error and does not infer neighboring tangency.
Sewing measures 17 interior native-pcurve checkpoints per shared edge, reporting
support-point gaps and unoriented tangent-plane angles. Neither these samples nor
the 33 guide checkpoints prove extrema between samples or global G1/G2.
Repairs here edit explicit inputs or add the missing authored face. Arbitrary
import healing, arbitrary self-intersections, general CAD parity and manufacturing
qualification remain outside this bounded B.03 evidence.

## Fresh local Studio verification

The 029B native build of runtime 0.9.30 passed the five new UI checks in 38.11
seconds: editor overflow, feature history, recovery, configurations and interaction
performance. All 15 selected retained workflows also passed in 45.03 seconds,
including canonical technical drawings at 1180 and 1440 pixels, drawing viewport,
compact visibility dock/local-model forms, stamps, plane sections, native vertex
pull, direct authoring, parametric authoring and provider connectors. The focused
sketch-diagnostics executable passed separately.

Capture inspection covered the 144/192 DPI overflow layouts and last-field edits,
configuration recipe and Manage entry, history actions, recovery prompt, canonical
drawing dimension form and sheet, compact Ollama form and drawing dock, and the
512-box interaction fixture. These checks observed usable controls and retained
current drawing/dock behavior. The DPI overrides do not qualify physical
cross-monitor transitions; model-provider fixtures do not establish live inference.

The interaction run retained 72 attempts, including nine warmups, with no failed
samples. Nearest-rank p95 Apply-to-`SwapBuffers` return was 33.752, 145.9965 and
390.2211 ms for 16, 128 and 512 boxes in that run. CPU/Windows/OpenGL identity,
render settings, runtime and fixture hashes, individual stages and raw samples are
in the generated report. These are local observations with no equivalent-baseline
improvement claim. Root integration runs may produce different measurements.

Receipts remain under ignored `out/cad-authoring`: `ui-port-verification.json`,
`focused-ui-1.log`, `retained-ui-1.log`, `sketch-diagnostics-run.log`, and the unique
interaction report under `ui-checks/interaction-performance-ui.png.interaction-performance/20261015T124123829Z-17556/`.
The first native build failure is retained in `port-build.log`: the in-progress
DXF helper needed its set include and separate mixed-type declarations. The
corrected build passed in `port-build2.log`. Generated inputs are synthetic;
the existing old Studio session was left open and untouched. No user design or
personal settings were written for these checks.

## Retained appearance and container boundaries

History rollback and selective loading retain embedded images, exact-face
appearance bindings and Stamp recipes. Metadata remains validated while geometry
is inactive. Stamp support discovery follows named datums, typed plane/axis/point
anchors, sketch anchors and direct/second feature dependencies; it does not assume
that `datum.parent` describes a datum with an explicit support recipe. Missing or
cyclic metadata rejects. A plane Stamp with an inactive source is omitted from
rendering until that source is active again.

Resuming evaluation rechecks exact topology. Editing inactive source geometry
does not silently rebind an observed `source_topology`: a stale face appearance
or body Stamp rejects resume atomically. Explicit removal and rebinding against
the current exact producer remain available. Format 40 roundtrips preserve the
existing format-39 image bytes, face appearances and Stamp source when the new
history/loading constructs are present; images alone still select format 39.

The initial v1 container checks rejected global authoring boundaries. The 029B
v2 followup now retains scoped configuration ownership and feature-owned pins;
child loading is workspace-local and does not overwrite the parent's selector.
Mixed-boundary/readonly writes and global history/Skeleton ownership still
reject explicitly. V1 compatibility and v2 persistence/remapping are covered by
[assembly scaling](ASSEMBLY_SCALING_029B.md); no global recipe is silently dropped.

Fresh native integration checks passed 4/4 in 11.15 seconds: appearance/Stamps,
design containers, and normal/compact Studio Stamp workflows. The native fixtures
cover format-40 JSON and binary roundtrips with embedded format-39 source, five
Stamp recipes, nested named/typed supports and a second-feature midplane,
rollback, stale-resume rejection, explicit rebinding, and unload/load-all behavior.
Container checks cover atomic command rejection, checkout/workspace/merge guards,
correctly rehashed baseline forgery and permitted empty arrays. Actual compact
viewport captures were inspected before and after supported-plane reactivation.

The first targeted attempt passed appearance checks but rejected a test fixture
whose source had been edited directly while retaining its prior immutable history
head. The fixture now loads independent format-40 authoring before the empty-array
checkout test. Both attempts are retained in `out/cad-authoring/appearance-container-tests1.log`
and `appearance-container-tests2.log`; the corrected test run does not replace that
failed attempt. Studio captures are `ui-checks/stamps-ui-compact.png.stamps-inactive-supported-plane.png`
and `ui-checks/stamps-ui-compact.png.stamps-resumed-supported-plane.png`.
