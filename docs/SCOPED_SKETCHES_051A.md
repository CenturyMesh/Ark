# Sketches owned by shared definitions — 051A

B.02/B.04: component sketch ownership is **Verificado no escopo** for the local
contracts recorded below. This does not qualify manufacturing or external CAD
interchange.

An assembly occurrence already identifies its shared definition by feature ID.
`NamedSketch.ownerFeature` uses that same stable ID. Zero is the legacy document
scope. Two definitions may each own `Profile`; repeated occurrences of either
definition reuse the same solved sketch. Ownership does not add a geometric
dependency from a sketch to the solid that consumes it.

```json
{"op":"sketch.set","owner_feature":12,"name":"Profile",
 "sketch":{"plane":"XY","entities":[
   {"kind":"circle","name":"Ring","dof":[0,0,10]}]}}
```

The canonical reference is `part:12::Profile`. Pass it explicitly in
`p.sketch`, native section/guide/path references, `source_sketch`, inspect
queries and context keys (`sketches:part:12::Profile`). Every sketch mutation
accepts `owner_feature` with a local `name`; an existing canonical reference
also works without that selector. No unqualified local name is searched in
other scopes. Unqualified names retain their legacy document meaning.

The owner must already exist. To author a result from its own sketch, create an
extrude definition with its ordinary initial profile, create the owned sketch,
then set that definition's `p.sketch` to the canonical reference. These may be
commands in one atomic batch. For a native solid, create its initial native box,
then change its native operation to extrusion once the owned profile exists.
There is no feature-type conversion or implicit reassignment of a sketch.

Definition renames leave IDs and canonical references intact. Removing a
definition with owned sketches rejects; explicitly remove its sketches first.
Referenced sketch deletion still fails normal dependency evaluation and rolls
back the batch. Removing an occurrence does not remove definition sketches.
There is no automatic sketch rename/rebinding operation.

Each definition and the legacy global scope permit 256 sketches, with 4096
total. Aggregate guards limit sketches to 32768 entities, 65536 constraints and
131072 scalar coordinates. Existing per-sketch and document-byte guards still
apply. Scoped local names use 1..96 characters, keeping canonical references
within the existing 128-character consumer contracts. New local/global names
cannot start with `part:`. Previously saved global names with this prefix load
unchanged; attempts to create a colliding scoped reference reject.

Format50 persists `owner_feature`, canonical `name` and `local_name`. Loading
verifies all three agree, the owner exists, names are unique and capacities
hold before replacing the destination document. Legacy documents do not acquire
owners or format50 merely by opening/saving. Existing feature/profile/project
references remain canonical strings throughout solver evaluation, history,
serialization, native operations and context. Context graph ownership edges are
labelled `owned_by_definition`, separately from geometry source edges.

The Studio sketch browser groups local labels under shared definitions and the
Document scope. Rectangle, circle and spline authoring expose an owner chooser;
existing sketch editing and curve appending use the canonical selection. Sketch
actions have a distinct 4096-entry range, separate from constraint and mate
actions. Design-container checkout retains unused owned sketches, and merge
remaps scoped references with their definitions without interpreting arbitrary
legacy names as references.

Verification targets: `scoped-sketches-051a` exercises 512 sketches in two
scopes, same-name isolation, self-owned extrusions, repeated occurrences,
per-scope and total overflow, aggregate resource guards, definition rename and
removal, exact persistence and legacy migration. `scoped-sketches-native-051a`
exercises cross-scope exact projection and extrusion, source edits, rejected
unqualified references/cycles/deletion, and reload. `design-containers-023b`
includes owned sketch retention and merge. Studio's `--self-test-tree-ui`
checks browser scrolling/selection; captures are local verification artifacts.
