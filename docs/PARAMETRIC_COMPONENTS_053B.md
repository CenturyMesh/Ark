# Parametric components and Neural Parametric View

053B addresses A.02/A.03/B.01/B.04/E.01. Reusable components retain authored
parameters, sketches, datums, features and assembly relationships. Geometry is
recomputed by ARK; a picture or a generated mesh is not the source definition.

## Files and ownership

| File | Meaning |
| --- | --- |
| `.ark` | Project, including embedded component definitions and local bindings. |
| `.arkprt` | One parametric part result with its dependency closure and local root. |
| `.arktop` | An assembly/subassembly recipe with its retained part definitions. |
| `.arkops` | Existing newline-delimited public-tool command replay. Unchanged. |

Component files use the owned binary ARK container. Their logical envelope is
`ark.parametric-component.v1`, with `component_id`, `revision`, `kind`, `name`,
`document`, `parameters` and `interfaces`. A subsequent revision also carries
`parent_sha256`. The embedded document contains authored source, not session
conversation, audit, usage or history. `parameters` lists public parameter names;
`interfaces` lists public named datums. Other dependency parameters remain
editable project source, but cannot be configured through the public interface.

Export is exclusive: it never overwrites an existing component file. Supplying
`previous_path` creates the next revision with the same component ID and the
previous digest. Insertion embeds the definition and its digest, maps numeric
identities, prefixes names and rewrites expression identifiers. Owned sketches
retain their local names and bind to the remapped feature definition. Different
namespaces can configure the same definition independently.

The first insertion preserves existing terminal bodies as assembly occurrences.
Source files are not consulted during project reopen or recomputation. The project
can be copied to another computer without the original component library. Embedded
definitions and bindings require semantic document version **51**; unrelated
documents retain their required earlier version. The physical container version
does not change.

## Studio

Open **Inspect → Graph**, or find **Neural Parametric View** in All tools under
**Components & library**. The same category exposes Export part, Export assembly,
Insert, Update and Configure. The graph toolbar provides those operations too.

Export selects a part feature ID or assembly group, a new file, comma-separated
public parameter/datum names, and optionally the previous revision file. Insert
selects a file and an identifier namespace. Public overrides are numeric JSON,
for example `{"width":30}`. ARK evaluates an isolated candidate and presents its
receipt for review before Apply. Cancelling either form leaves the project alone.

Update requires the next revision in the exact digest chain. It preserves target
IDs, numeric public overrides, and independent placement/visibility/name of the
component root. Changes to internal authoring are detected and rejected rather
than overwritten. Export a deliberately edited definition for review when the
installed source has diverged. Other copies and projects remain on their pinned
revision until explicitly updated.

The graph uses the same local authored relation index as `context_graph`:

- Select a part or occurrence to select its geometry in Studio. Selecting a
  parameter highlights known dependants; the inspector shows authored facts.
- Double-click a part or group to fold/unfold its detail. Search includes direct
  neighbours; Focus limits the map around the selected object.
- Drag nodes to arrange the view, drag the background to pan, use the wheel to
  zoom, F to fit and Escape to close.
- Colours distinguish object kinds. Links distinguish dependency, assembly and
  embedded component reuse; reuse is dashed. Thumbnails use current display meshes.

Layout and navigation do not edit geometry or call a model. This is a partial
authored relationship index, not a complete evaluator dependency graph, neural
network, manufacturing proof or measured token saving.

## Public tool flow

```json
{"tool":"parametric_components","arguments":{"action":"export","kind":"part","feature":1,"name":"Bracket","path":"bracket-r1.arkprt","parameters":["width"],"interfaces":["Mount"],"expected_revision":7}}
{"tool":"parametric_components","arguments":{"action":"prepare","mode":"insert","path":"bracket-r1.arkprt","namespace":"left","overrides":{"width":30},"expected_revision":3}}
```

Read the actual returned `candidate_id`, then commit using
`doc_apply{candidate_id,expected_revision}`. No placeholder ID is usable. Prepare
freezes the loaded package, evaluates exact geometry and does not publish edits.
A stale candidate rejects. `mode:update` takes the next file and installed
namespace; `mode:configure` takes the namespace and public overrides, without a
source file. `action:inspect` reads installed manifests or an explicit file.

The underlying atomic commands are `component.insert`, `component.update` and
`component.configure`. They require `expected_revision`. Insertion/update carries
an embedded `package` and matching `package_sha256`; update/configure additionally
requires `expected_package_sha256` for the installed revision. Ordinary preview,
postcondition, undo/redo and publication rules apply.

## Current boundaries

- An assembly export retains its materialized editable recipes. It does not yet
  preserve a hierarchy of independently updateable nested library bindings.
- Revision replacement requires the same source identity sets. Adding/removing
  source objects or renaming named dependencies requires insertion under a new
  namespace and explicit reconnection; there is no silent topology rebase.
- Global Skeleton/motion/drag-chain/configuration/external-reference ownership,
  imported source payloads, BOM part metadata and unsupported image appearances
  need separate remapping contracts and reject. Cross-boundary assembly mates
  reject. Export from an isolated checkout is unsupported.
- Drawings, canvases, exploded display layouts, original project parent placement
  and unrelated objects are outside the component recipe. Selected subassemblies
  are exported without their parent frame; retained local placement remains explicit.
- Scoped designer checkouts receive only their materialized source closure, not
  the owning project's embedded component packages. Result integration is still
  governed by the checkout contract; a changed component recipe is then a local
  divergence for future library revision updates.
- Existing document/geometry limits apply, including 4 MiB logical persistence,
  256 parameters, 1024 features and at most 128 component bindings. Namespace
  prefixes count towards authored name limits. No hosted registry or background
  update service is introduced.

Verification is recorded in [053B](iterations/053B.md).
