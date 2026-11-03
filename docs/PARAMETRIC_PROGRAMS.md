# Parametric programs

Parametric programs address **B.02**, **D.02** and **G.01** within a bounded
local scope; each milestone remains **Parcial**. A program is design source
written as code. It compiles public parameter values into ordinary native
authoring commands under one namespace. ARK still evaluates every feature with
its exact kernel, keeps `expected_revision` and publishes atomically. The
program never produces B-rep, meshes or files, and it never runs scripts,
external programs or network requests.

## When to use a program

| Need | Use |
| --- | --- |
| A family whose structure follows from parameters: counts, conditional parts, generated sketches (shelving, racks, grids, bolt circles, ladders) | A parametric program |
| Global driver relations, vector arrays or patterns of an existing seed feature | [Skeleton code](SKELETON_AUTHORING.md) |
| A reusable authored part or assembly shared between projects | [Parametric components](PARAMETRIC_COMPONENTS_053B.md) |
| Human-adjustable named dimensions with ranges | [Parametric HUD](PARAMETRIC_HUD_048B.md) |

## Source format

```json
{
  "format": "ark.parametric-program.v1",
  "name": "Bookshelf",
  "description": "optional",
  "parameters": {
    "width":   {"default": 800, "min": 300, "max": 2000, "unit": "mm"},
    "shelves": {"default": 5, "min": 2, "max": 12, "integer": true}
  },
  "functions": {
    "shelf": {"params": ["k", "pitch"], "body": [
      {"let": "half", "value": "width/2-thickness"},
      {"emit": {"op": "sketch.set", "name": "shelf{k+1}", "sketch": {"plane": "XZ", "entities": [
        {"kind": "wire", "name": "outline", "start": ["{-half}", "{k*pitch}"], "segments": [
          {"kind": "line", "to": ["{half}", "{k*pitch}"]},
          {"kind": "line", "to": ["{half}", "{k*pitch+thickness}"]},
          {"kind": "line", "to": ["{-half}", "{k*pitch+thickness}"]},
          {"kind": "line", "to": ["{-half}", "{k*pitch}"]}]}]}}},
      {"emit": {"op": "feature.add", "type": "native_solid", "name": "Shelf {k+1}",
                "native": {"op": "extrude"}, "p": {"sketch": "shelf{k+1}", "depth": "{depth}"}}}
    ]}
  },
  "body": [
    {"let": "pitch", "value": "(height-thickness)/(shelves-1)"},
    {"assert": "pitch-thickness>=120", "message": "Clear gap between shelves must be at least 120 mm"},
    {"for": "i", "count": "shelves", "body": [{"call": "shelf", "args": {"k": "i", "pitch": "pitch"}}]}
  ]
}
```

The complete bookshelf and a bolted flange are returned by
`parametric_program{action:"examples"}` and by `agent_onboard{topic:"programs"}`.

Parameters are immutable finite numbers. `default` is required; `min`, `max`
and `integer` bound the public values, which are validated before every run.
Program variables are numbers only.

### Statements

Each statement is an object with exactly one keyword:

| Statement | Meaning |
| --- | --- |
| `{let:x, value:expr}` | Binds `x` in the current block. A parameter name cannot be rebound. |
| `{set:x, value:expr}` | Updates the nearest earlier `let`, for example an accumulator. |
| `{for:i, count:expr, body:[...]}` | Runs `i = 0..count-1`; the count is an integer 0..1024. |
| `{if:expr, then:[...], else?:[...]}` | Nonzero is true. |
| `{assert:expr, message}` | Rejects the run with the message when false. |
| `{call:fn, args:{param:expr}}` | Runs a function. It sees the parameters and its own arguments only, and every declared argument must be passed. |
| `{emit:command}` | Outputs one native command after template expansion. |

### Expressions

Program expressions support `+ - * / % ^`, comparisons `< <= > >= == !=`
(result 1 or 0, not chained), `&& || !`, `cond ? a : b`, the constants `pi`,
`e`, `true` and `false`, and the functions `sin cos tan asin acos atan atan2
sqrt abs floor ceil round trunc sign exp log log10 hypot pow mod min max clamp
lerp deg`. Trigonometry uses radians and `deg(x)` converts degrees to radians,
as in document expressions. `%` and `mod` return a result with the sign of the
divisor. A non-finite result rejects the run.

### Templates

Inside an emitted command:

- A string that is exactly `{expr}` becomes a JSON number, except for sketch
  coordinates (`dof`, wire `start`/`to`/`c1`/`c2`, `radius`, `val`, `val2`
  inside `sketch.set`). ARK treats a numeric sketch coordinate as a free solver
  variable, so placeholders there stay driven text such as `"445.5"`. Write
  literal sketch coordinates as text (`"0"`) to drive them exactly as well.
- Other text with placeholders interpolates the shortest round-trip number,
  for example `"Shelf {i+1}"`. `{{` and `}}` are literal braces.
- A string without braces is passed unchanged. A document parameter expression
  such as `"thickness*2"` therefore stays associative with the document.
- Inside an emitted array, `{"$for":"k","count":expr,"item":{...}}` splices
  `count` items, for example the circles of a bolt pattern in one sketch.

## Emitted commands and namespaces

Programs emit only `feature.add`, `sketch.set`, `plane.set` and `material.set`,
each with an explicit local `name` of 1..96 bytes, unique per kind. ARK
namespaces every name:

| Kind | Published name |
| --- | --- |
| Feature | `<namespace> / <name>` |
| Sketch, datum, material | `<namespace>__<name>` |

References to program-local names are rewritten by field meaning: feature
`inputs`, `p.sketch`, `p.plane`, native `sections`, `guides`, `path`, `profile`
and `curve`, plane `parent` and `support`, and `material`. Names that are not
program-local refer to existing project objects unchanged, so a program can
build on `XY` or on a datum the user owns. Numeric feature IDs, `id` fields and
`owner_feature` sketches reject: IDs are not stable program identities.

Objects in a program namespace belong to that program. A namespace cannot reuse
the prefix of an existing object or a parametric component.

## Tool and publication

`parametric_program` has four actions:

| Action | Effect |
| --- | --- |
| `validate{program, values?, namespace?}` | Compiles without the document. Returns the commands, owned names, resolved values, passed assertions and steps. Read-only. |
| `examples{}` | Returns the built-in example programs. Read-only. |
| `inspect{namespace?, include_program?}` | Lists bindings with values, owned names and `local_edits`. Read-only. |
| `prepare{mode, namespace, program?, values?, expected_revision}` | Evaluates one isolated exact candidate with `doc_preview`. |

Prepare modes:

| Mode | Candidate |
| --- | --- |
| `install` | Emitted commands plus `program.bind` in a new namespace. |
| `configure` | `program.clear`, the installed source re-run with merged values, `program.bind`. |
| `update` | `program.clear`, new source with the retained values it still declares, `program.bind`. |
| `remove` | `program.clear`: deletes owned objects and the binding. |
| `detach` | `program.detach`: keeps the objects as ordinary editable source and drops the binding. |

Publish the returned candidate with `doc_apply{candidate_id, expected_revision}`.
A stale candidate rejects. The `program.*` operations are also ordinary
`doc_apply` commands and always require `expected_revision`.

```json
{"tool":"parametric_program","arguments":{"action":"prepare","mode":"configure","namespace":"shelf","values":{"shelves":4},"expected_revision":12}}
{"tool":"doc_apply","arguments":{"candidate_id":"<returned id>","expected_revision":12}}
```

`agent_onboard{topic:"commands",query:"program"}` also offers the
`program_configure` recipe, which prepares the same call without executing it.

## Ownership, local edits and dependants

`program.bind` re-compiles the reviewed source from its digest, checks that
every owned object exists exactly once, and stores a fingerprint of each owned
row as the binding baseline. The baseline is resealed after the evaluated batch.

A re-run or removal:

- rejects `PROGRAM_LOCAL_EDITS` when an owned object was edited, renamed or
  removed after the run. Detach to keep the edits as ordinary source.
- rejects `PROGRAM_DEPENDANTS` when unrelated source still names or numbers an
  owned object: feature inputs, assembly occurrences, scoped sketches, datum
  supports, materials, drawings or Skeleton source text. Remove or retarget the
  dependency, or detach.
- rejects `PROGRAM_STALE_PROGRAM` when the installed source changed after the
  candidate was prepared.

There is no silent rebase, tolerance widening or discarded geometry. A failed
emitted feature rejects the whole candidate with the evaluator's error.

## Persistence

Bindings are stored in the `.ark` source as `parametric_programs`: namespace,
name, the full program, its SHA-256, resolved values, owned names and baselines.
They require conditional semantic document version **55**. Documents without a
binding keep their earlier required version; removing the last binding returns
the document to its earlier version. A tampered digest, incomplete values or
overlapping ownership reject on load without replacing the open document.
Scoped design-container checkouts and component exports do not carry bindings;
the owning project keeps them. The physical container version does not change.

## Team role

`AgentTeamDiscipline::ParametricDesigner` ("Parametric designer") writes only
batches that carry a `program.*` operation, plus the four creation operations a
program compiles to. Its instructions load the `programs` topic, validate,
prepare in the assigned namespace, publish its own candidate with
`team_apply_step`, inspect native geometry and report namespace, values, owned
names and assertions to the orchestrator. A lead in the same team is told to
delegate counted, repeated or conditional families to it. Other writers can
also use programs; the physics reviewer can validate and inspect only.

A program candidate prepared by a team role becomes that role's retained
candidate. It still passes the role's step scope and the configured
command/feature/placement limits; Studio allows 32 features per step, so split
larger families into several namespaces. Studio's default three-role team is
unchanged and does not yet offer this role in its role cards.

## Limits

| Limit | Value |
| --- | --- |
| Source size | 256 KiB |
| Statements | 8192; blocks nest to 32 |
| Evaluation steps | 50000 per run |
| Loop count | 1024 per loop or `$for` |
| Nested calls | 16 |
| Emitted commands | 126 per run, at most 2 MiB |
| Parameters, functions | 64 each; 16 arguments per function |
| Bindings | 64 per document |
| Expression text | 2048 bytes |

## Evidence and limits

The local `parametric-programs` CTest covers the language (precedence, logic,
accumulators, templates, `$for` splices and typed rejections), namespacing and
reference rewriting, install/configure/update/remove/detach candidates, stale
candidates, local edits and dependants, undo/redo, save/reopen at version 55,
downgrade and digest rejection, tool discovery and onboarding examples. A
generated shelf is checked against an independent exact volume.

Assertions and bounds encode the design rules stated by the author. They are
not manufacturing, structural or fit qualification. No measured agent quality,
time or token benefit is claimed. The `.arkprog` file exchange, Studio editing
controls and context-graph nodes for bindings are not implemented.
