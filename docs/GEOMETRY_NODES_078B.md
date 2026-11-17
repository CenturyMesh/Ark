# Editable geometry nodes — 078B

**B.01 / B.02 / D.02 — Verificado no escopo local**: native authoring/custody/
persistence tests and real Studio node/preview controls passed on Windows.

Open **Geometry Nodes** from All tools → Components & library, or press
**Nodes** in the existing Parametric Graph window. Both views navigate the same
document; there is no separate geometry program or shadow copy of the model.

The canvas has mesh, exact surface and solid output ports, indexed input ports,
visible wires, zoom/pan, movable cards and an inspector. **Add node** offers
solid primitives and operations, an editable surface patch with offset/thicken/
sew, a parametric mesh network, mesh processing and explicit exact-to-mesh
sampling. Ctrl-click source nodes before adding an operation; their authored
feature order supplies its input list. Drag an output port onto an input port to
reconnect it. Double-click or **Edit node** edits the existing source. The model
keeps its feature IDs and normal undo/redo history.

Editing or reconnecting first opens **Review geometry edit** with a rendered
snapshot of the checked candidate, changed-node names, and the preparation's
observed evaluated/reused counts and feature-recompute time. **Apply** publishes
that exact candidate without a second geometry pass. **Cancel** releases its
retained candidate and temporary image and preserves the current source/revision.
An intervening document change rejects Apply; the preview is never silently
rebased. A checked selection with no display faces is stated explicitly instead
of showing a fabricated image. Preview pixels remain display evidence.
For an existing target, **Before / After** uses one custom camera derived from
the union of its measured current/candidate bounds. Both actual snapshots keep
the same camera, dimensions and image scale, so translations remain visible
instead of disappearing through independent auto-framing. New-node and removal
reviews state their corresponding single-result/remaining-model scope.

## Agent contract

- `geometry_graph{action:"catalog"}` returns source/operation recipes and typed
  input/output contracts.
- `geometry_graph{action:"inspect"}` returns nodes, input sockets, links and the
  current revision. This is source navigation, not manufacturing qualification.
- `geometry_graph{action:"prepare",expected_revision,edit:{...}}` compiles one
  node edit to existing `feature.add`, `feature.edit` or `feature.remove` commands
  and evaluates a normal isolated `doc_preview` candidate. Publish only through
  `doc_apply{candidate_id,expected_revision}`.

Preparation requests `detail:"changes"` internally: before/after rows are an
explicitly partial changed-node summary, and the candidate receipt retains its
observed rebuild counters. Unrelated definitions are omitted from the response;
the retained candidate and its snapshots still contain the complete checked model.

Edits are `add{recipe,name?,inputs?,parameters?,native?}`, `edit{node,name?,
parameters?,native?}`, `link{source,target,input}`, `disconnect{target,input}` and
`remove{node}`, with the action name in `edit.op`. Input socket indices start at
zero. Preparation does not change the current document. The Studio gesture
prepares the exact candidate for human review and retains its revision guard
through explicit Apply/Cancel.

The native host's `discardPreparedCandidate` lifecycle operation removes only
the selected retained candidate; it is not an agent geometry mutation. Unknown
or already-consumed IDs are harmless. Shared bounded native caches may still
own reusable geometry after unique preview ownership is released.

## Authority and limits

Mesh remains triangle authority. Mesh-to-solid conversion is not inferred.
`sample_to_mesh` explicitly uses the existing `p.sample=1` conversion and its
result carries no exact B-rep. Current native B-rep checks still govern solids
and surfaces; socket compatibility alone is insufficient for successful CAD.

Connections follow the document's authored feature order. Self-links, forward
links/cycles, duplicate sources, missing/suppressed sources, wrong domains and
missing required inputs reject without publishing a partial graph. A required
unary port cannot be disconnected into a saved broken operation: reconnect it
or remove the consumer. Variadic operations may disconnect only while retaining
their minimum inputs. The editor does not silently reorder history or detach
program/component ownership. Existing ownership semantics are retained: a manual
change/removal of a program output remains a local edit of that binding;
`parametric_program inspect` reports the divergence and a later configure/update
rejects it until the edit is undone or the program is explicitly detached. The
node editor does not quietly reseal generated source. Specialized existing feature types can remain
visible with read-only connections; use their specialized editor.

Source parameters and links persist through the existing `.ark` feature schema.
No document-format bump is introduced. Canvas positions and selection remain
session UI state; they do not change model geometry or revision.
Nodes edit feature definitions. Placing or rebinding assembly occurrences remains
an explicit assembly operation; the node editor does not silently move instances.
