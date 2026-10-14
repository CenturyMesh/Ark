# 023B manual testing

023B / runtime 0.9.24 extends the 022B draft. User acceptance and merge remain
pending. Use a copy of a project when comparing versions; new import, holes or
container recipes require source format 38 and cannot be opened by older builds.
Existing projects without those recipes keep their applicable source format.

The local immutable preview includes the native worker and dependency notices.
From the 023B checkout:

```powershell
& .\out\test-preview\023B-test1\ark-studio.exe
```

For a fresh source build, run `out/native-release/ark-studio.exe` after the native
build workflow. The local full suite passed 115/115; affected visual controls
were subsequently rechecked at 1440×900 and 1366×768. These results do not replace
manual user acceptance. [The receipt](verification-023B.json) records hashes.

## STEP/IGES and Arkitfy

Use **Import STEP / IGES** from the command gallery, or drop a `.step`, `.stp`,
`.iges` or `.igs` file onto Studio. Choose **Just import** and a reference name.
Check placement, multiple solids or surfaces, selection and downstream native
operations. Save and reopen the `.ark` after moving the original exchange file:
its source and transferred geometry should remain available. Canceling the
choice must leave the document unchanged.

Repeat with **Arkitfy using agents**. The Assistant opens with a reconstruction
task; a configured available agent can execute it. Without configuration, the
task remains ready to send. Review the resulting native tree and exact comparison
evidence. The import alone is not a completed reconstruction. Surface-only or
unsupported comparisons report their limits instead of claiming equivalence.

## Holes Orchestrator

Create/select a solid, then choose **Holes Orchestrator**. Start with metric M6
clearance, through termination and a location entering the solid. Inspect its
feature tree, then edit the same feature to change size, position or blind depth.
Add several named locations with the locations editor; expressions should follow
source parameters. Create a second holes feature on the first result to check
that names remain unambiguous. Undo/redo and save/reopen should preserve recipes.

Try tapped preparation, counterbore/countersink, a dowel and a shaft/bore groove
on suitable stock. Thread designations are semantic; this scope does not generate
helical thread flanks. Values without published defaults must be explicit. Test
an excessive blind depth or recess: rejection must preserve the original solid.
Numeric fit tolerances are not inferred from an unspecified fit requirement.

## Design containers

Create a container around an assembly occurrence using **Create design
container**. Check that its placement stays unchanged. Inspect its editable and
read-only source closure. **Check out design container** writes a new `.ark`;
the option to open another ARK starts an independent document session. Existing
files must not be overwritten.

Edit an owned dimension in the child and save it. In the parent, choose **Merge
edited design container**, review the commands and apply. Unrelated parent source
must remain unchanged. Canceling either dialog must not merge. Repeat with a
parent changed since checkout or a child changed after review: the merge must
reject. Unsupported boundary references are explicit limitations; this is not
general distributed merging or an unlimited document-size claim.

Agents create, edit, refresh or export technical drawings only when explicitly
requested. None of these workflows should create drawings automatically.

## Evidence and scope

See [iteration record](iterations/023B.md), [import](CAD_IMPORT_023B.md),
[holes](HOLES_ORCHESTRATOR_023B.md), [containers](DESIGN_CONTAINERS_023B.md) and
[source research](RESEARCH_023B.md). Local synthetic contracts, desktop checks,
hosted CI, live agent reconstruction and user acceptance are separate evidence.
