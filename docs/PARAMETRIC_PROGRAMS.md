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

The complete bookshelf, a bolted flange and a turned knob are returned by
`parametric_program{action:"examples"}` and by `agent_onboard{topic:"programs"}`.
The first two are straight extrusions. The knob revolves one half profile (lines,
two cubic wire spans and a quarter arc) with `revolve_surface` `p.solid:1` into
one exact solid with curved faces. Examples show the language; they do not
prescribe the form of a design.

Parameters are immutable. `default` is required, and every public value is
validated against its parameter's kind before every run. Program variables are
numbers only.

### Parameter kinds

| Kind | Declaration | Accepted values | Stored value | In expressions |
| --- | --- | --- | --- | --- |
| Number | `{"default":800, "min":300, "max":2000, "integer":false, "unit":"mm"}` | A finite number within `min`/`max`, integral when `integer` is true | The number | The number |
| Boolean | `{"boolean":true, "default":false}` | `true`, `false`, `0` or `1` | `1` or `0` | `1` or `0` |
| Choice | `{"choices":[6,8,10], "default":8, "unit":"mm"}` | One listed number | The listed number | The number |
| Text choice | `{"choices":["Oak","Pine"], "default":"Oak"}` | One listed text, matched exactly | The listed text | Its 0-based index |

`choices` lists 1..64 distinct values, either all finite numbers or all text
of 1..64 bytes without braces or control characters. A typed parameter fixes
its own domain, so `min`, `max` and `integer` reject with `boolean` or
`choices`; a boolean or text choice has no `unit`. A text choice is a number in
expressions (`finish==1` selects `"Pine"` above) and is inserted as text in
emitted templates with `{@finish}`, so a program can choose a material, a
name or another text field:

```json
{"emit": {"op": "feature.add", "type": "native_solid", "name": "Panel {@finish}",
          "material": "{@finish}", "native": {"op": "box"},
          "p": {"width": "{width}", "depth": 18, "height": "{rail ? 40 : 20}"}}}
```

`{@name}` accepts only a text-choice parameter and is checked statically like
other names, including in branches that do not run; `{@width}` on a number or
a function argument rejects `PROGRAM_TEMPLATE`. A text that is not a
program-local name refers to an existing project object, as for any other
emitted reference. `validate`, `inspect` and the compilation report list
`parameter_kinds`.

### Statements

Each statement is an object with exactly one keyword:

| Statement | Meaning |
| --- | --- |
| `{let:x, value:expr}` | Binds `x` in the current block, after its value is evaluated. A parameter name cannot be rebound. |
| `{set:x, value:expr}` | Updates the nearest earlier `let` in scope, for example an accumulator. Parameters, `for` variables and function arguments are read-only. |
| `{for:i, count:expr, body:[...]}` | Runs `i = 0..count-1`; the count is an integer 0..1024. |
| `{if:expr, then:[...], else?:[...]}` | Nonzero is true. |
| `{assert:expr, message}` | Rejects the run with the message when false. |
| `{call:fn, args:{param:expr}}` | Runs a function. It sees the parameters, its own arguments and its own `let`s only, and every declared argument must be passed. Calls nest to 16, and the blocks of the body and all active calls to 64; a deeper run rejects `PROGRAM_RECURSION`. |
| `{emit:command}` | Outputs one native command after template expansion. |

Names are lexical, as at run time. A `let` is visible to the later statements
of its block and to their nested blocks; a `for` variable inside its body; a
`$for` variable inside its `item`; a function body sees the parameters, its
arguments and its own `let`s, not the caller's. Compilation (`validate`,
`prepare` and `program.bind`) resolves every identifier in expressions and
`{...}` placeholders, every function call and its argument count, and every
`set` target before anything runs, including branches that the current values
do not take. A typo rejects with its path, for example
`PROGRAM_EXPRESSION: body[0].then[0].value: unknown identifier 'widht'`.

### Expressions

Program expressions support `+ - * / % ^`, comparisons `< <= > >= == !=`
(result 1 or 0, not chained), `&& || !`, `cond ? a : b`, the constants `pi`,
`e`, `true` and `false`, and the functions `sin cos tan asin acos atan atan2
sqrt abs floor ceil round trunc sign exp log log10 hypot pow mod min max clamp
lerp deg`. Trigonometry uses radians and `deg(x)` converts degrees to radians,
as in document expressions. `%` and `mod` return a result with the sign of the
divisor.

`cond ? a : b` evaluates only the selected branch, `a && b` skips `b` when `a`
is 0, and `a || b` skips `b` when `a` is nonzero, so guards such as
`x!=0 ? 1/x : 0` and `n>0 && total/n>2` work for every value. Skipped operands
are still checked for syntax, names and argument counts.

Every value computed while a program runs must be finite. Division or modulo
by zero, overflow such as `10^400` or `exp(1000)`, a literal such as `1e999` and
a function outside its domain such as `sqrt(-1)` or `log(0)` reject
`PROGRAM_NONFINITE` with the statement path and the expression. This holds
inside comparisons, `&&`, `||`, `!`, conditions, assertions, loop counts and
function arguments, so `1/(a-b) > 0` or `min(1/0, 3)` cannot turn an infinity
into a truth value or a finite number. Code that is not run, for example the
untaken branch of an `if`, is checked for syntax and names only.

Parentheses, function arguments, `?:` and chains of unary `- + !` or `^` share
one nesting limit of 64 levels; deeper text rejects `PROGRAM_EXPRESSION`.

### Templates

Inside an emitted command:

- A string that is exactly `{expr}` becomes a JSON number, except for sketch
  coordinates (`dof`, wire `start`/`to`/`c1`/`c2`, `radius`, `val`, `val2`
  inside `sketch.set`). ARK treats a numeric sketch coordinate as a free solver
  variable, so placeholders there stay driven text such as `"445.5"`. Write
  literal sketch coordinates as text (`"0"`) to drive them exactly as well.
- Other text with placeholders interpolates the shortest round-trip number,
  for example `"Shelf {i+1}"`. `{{` and `}}` are literal braces.
- `{@name}` inserts the chosen text of a text-choice parameter, for example
  `"material":"{@finish}"`. A string containing it is always text.
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
`owner_feature` sketches reject: IDs are not stable program identities. A key
that accepts a sketch or a feature (`sections`, `section`, `guides`, `path`,
`profile`, `curve`) rejects `PROGRAM_REFERENCE` when the program emits both a
sketch and a feature with that local name; give them distinct names.

Objects in a program namespace belong to that program. A namespace cannot reuse
the prefix of an existing object or a parametric component.

## Tool and publication

`parametric_program` has five actions:

| Action | Effect |
| --- | --- |
| `validate{program or program_path, values?, namespace?}` | Compiles without the document. Returns the commands, owned names, resolved values, `parameter_kinds`, passed assertions and steps. Read-only. |
| `examples{}` | Returns the built-in example programs. Read-only. |
| `inspect{namespace?, include_program?}` | Lists bindings with values, `parameter_kinds`, owned names and `local_edits`. Read-only. |
| `prepare{mode, namespace, program or program_path?, values?, expected_revision}` | Evaluates one isolated exact candidate with `doc_preview`. |
| `export{namespace, path, expected_revision, overwrite?}` | Writes the installed source to a `.arkprog` file. The document is unchanged. |

`values` maps parameter names to numbers, booleans or choice text, according
to each parameter's kind.

Prepare modes:

| Mode | Candidate |
| --- | --- |
| `install` | Emitted commands plus `program.bind` in a new namespace. |
| `configure` | `program.clear`, the installed source re-run with merged values, `program.bind`. |
| `update` | `program.clear`, new source with the retained values it still declares, `program.bind`. |
| `remove` | `program.clear`: deletes owned objects and the binding. |
| `detach` | `program.detach`: keeps the objects as ordinary editable source and drops the binding. |

Publish the returned candidate with `doc_apply{candidate_id, expected_revision}`.
A stale candidate rejects; program candidates do not support `refresh_candidate`
and must be prepared again. The `program.*` operations are also ordinary
`doc_apply` commands and always require `expected_revision`.

A `program.bind` publishes exactly its compiled source. In the same batch it
must be immediately preceded by the commands its program, values and namespace
compile to (canonical JSON, same order), optionally after one `program.clear`
of that namespace. No other command of the batch may create, edit, rename or
remove an object in that namespace, a namespace is bound at most once per
batch, and a compiled object must not reuse the name of an existing row that
the cleared binding did not own, since an upsert would merge into it. A later command that
edits an owned object by numeric ID is caught by the next evaluation of the
batch, including the intermediate evaluations that `assembly.drive`,
`component.*`, `motion.graph.set`, `motion.bake` and `drag_chain.set` run.
Violations reject `PROGRAM_PROVENANCE` and leave the document unchanged. The
`prepare` candidate (`[program.clear?, compiled commands, program.bind]`) and
the exact `validate` output followed by its bind satisfy this contract.

```json
{"tool":"parametric_program","arguments":{"action":"prepare","mode":"configure","namespace":"shelf","values":{"shelves":4},"expected_revision":12}}
{"tool":"doc_apply","arguments":{"candidate_id":"<returned id>","expected_revision":12}}
```

`agent_onboard{topic:"commands",query:"program"}` also offers the
`program_configure` recipe, which prepares the same call without executing it.
Its `values` take numbers, `true`/`false` and choice text of 1..64 bytes, as
the tool does; the program's declarations still decide which value is valid.

## Program files (`.arkprog`)

A `.arkprog` file exchanges program source between projects. It is canonical
JSON (sorted keys, no whitespace) of at most 256 KiB:

```json
{"format":"ark.parametric-program-file.v1","program":{...},"program_sha256":"<64 hex>"}
```

`export{namespace, path, expected_revision}` writes the installed program and
its SHA-256 to `path`, which must be absolute and end in `.arkprog`. The bytes
go to a staged file in the destination directory and are published by one
rename. An existing file is never replaced unless `overwrite:true` is passed,
and the check is part of the rename, so a file created concurrently is not
replaced either. The result reports `program_sha256`, the file's own
`file_sha256`, `bytes` and `replaced`; the document and its revision do not
change.

`validate` and `prepare` accept `program_path` instead of `program`. The file
must be a regular `.arkprog` file of at most 256 KiB with the expected format,
only the three fields above and a `program_sha256` matching its program;
otherwise it rejects `PROGRAM_FILE`, `PROGRAM_CAPACITY`, `PROGRAM_FORMAT`,
`PROGRAM_SYNTAX` or `PROGRAM_HASH`. The source is then validated, compiled
and evaluated exactly like an inline program, and the reviewed candidate
binds the same digest. The file carries no values; pass `values` as usual.
Bound values, owned objects and baselines stay in the project.

Only the `parametric_program` tool reads or writes these files, and only at
the explicit path. Program code still never reads files, runs scripts or
external programs, or uses the network.

## Ownership, local edits and dependants

`program.bind` re-compiles the reviewed source from its digest, checks that
every owned object exists exactly once, and stores a fingerprint of each owned
row as the binding baseline. Before each evaluation in the batch, the rows must
still match the baseline (`PROGRAM_PROVENANCE` otherwise); each evaluation then
reseals it, and the batch keeps checking its bindings until `doc_apply`
publishes it. `local_edits:false` therefore means the owned rows are the
evaluated compiled output. Edits in later batches are ordinary local edits.

A re-run or removal:

- rejects `PROGRAM_LOCAL_EDITS` when an owned object was edited, renamed or
  removed after the run. Detach to keep the edits as ordinary source.
- rejects `PROGRAM_DEPENDANTS` when unrelated source still names or numbers an
  owned object: feature inputs, assembly occurrences, scoped sketches, datum
  supports, materials, drawings or Skeleton source text. Remove or retarget the
  dependency, or detach. The Skeleton check is a conservative text search: an
  owned name anywhere in the Skeleton source, including comments and JSON
  escaped quoted names such as `"q / Post \u0022A\u0022"`, blocks the run.
- rejects `PROGRAM_STALE_PROGRAM` when the installed source changed after the
  candidate was prepared.

There is no silent rebase, tolerance widening or discarded geometry. A failed
emitted feature rejects the whole candidate with the evaluator's error.

`context_graph` indexes each binding as node `program:<namespace>` of kind
`program`, with the program name and facts `namespace`, `program_sha256` and
resolved `values`, and `owns` links to the owned features, sketches, datums
and materials that currently exist under a unique name. Configuring or
updating the program changes the node's fingerprint; detaching it removes the
node. The graph is navigation only: `parametric_program{action:"inspect"}`
remains the authority for `local_edits`. Studio's Parametric graph shows the
same nodes; `owns` is ownership metadata, so selecting an owned object does
not highlight its binding as an impacted dependant.

Programs cannot emit assembly occurrences or mates. Mates have no name, only
document-assigned numeric IDs, while program ownership, baselines and
reference rewriting are name-based, and the dependants check follows names
and feature IDs, not the occurrence IDs that mates, cable endpoints, exploded
views, motion and configurations use. Place generated parts in an assembly
with ordinary `assembly.add`/`mate.add` commands; such references make a
re-run reject `PROGRAM_DEPENDANTS` until they are removed, or the program is
detached.

## Studio

Installed programs are configured in the [Parametric HUD](PARAMETRIC_HUD_048B.md)
(**HUD** in the Model or Manage ribbon). When the document has a binding,
the HUD shows a **Parameters / Programs** switch under its header. It opens on
**Programs** when no document parameter is exposed. The
[Design variants](DESIGN_VARIANTS_067G.md) window (**Manage > Variants** or
**All tools > Parametric configurator**) remains the place for named
configurations, comparison and exploration; the HUD's **Variants** button opens
it on the same namespace.

- **Bindings.** After each revision the view reads
  `parametric_program{action:"inspect"}`: namespace and name, owned objects and
  `local_edits` (shown in amber). **<** and **>** step through the installed
  programs. Studio runs this inspect on a private session that holds only the
  bindings and their owned objects. Each owned row is serialized from its own
  object, so the rows and `local_edits` are the same as the live inspect. The
  rest of the drawing, such as imported meshes, is not serialized on the UI
  thread. If that session cannot answer, the live session does.
- **Values.** Each declared parameter has a row with its value and `unit`, the
  range, **-**/**+** and a value box for typed entry. Typed entry accepts a
  number or an expression of document parameters, evaluated once. Integer
  parameters step by 1 and are rounded. Other bounded parameters step by a power
  of ten near a twentieth of the range. The HUD clamps to `min`/`max` and names
  the limit; agents are rejected instead. Units are labels only. A value that is
  not a number is shown read-only.
- **Apply.** Changes are staged in amber with `was <value>`. **Apply (n)** sends
  `prepare{mode:"configure",namespace,values,expected_revision}`, then
  `doc_apply{candidate_id,expected_revision}` with
  `actor:{kind:"human",id:"studio"}`. Both calls run on Studio's CAD worker
  path, so the viewport keeps painting. The result line shows the new revision
  and the measured prepare and publish times.
- **Stale values.** Values belong to the revision of the rows they were staged
  from. A value typed while another writer changed the model (for example from
  the Design variants window) is not staged: the rows are read again and the
  HUD asks for the value again. Values staged at an older revision are not
  published. The first **Apply** reports the revision change, and whether the
  program source changed. A second **Apply** prepares them on the current
  model; **Reset** discards them.
- **Rejections.** A rejection keeps the document, the revision and the staged
  values. The HUD shows the code and message: `PROGRAM_LOCAL_EDITS`,
  `PROGRAM_DEPENDANTS`, `PROGRAM_STALE_PROGRAM`, `PROGRAM_ASSERT` with the
  author's message, `PROGRAM_VALUES`, or the evaluator error of a failed
  candidate. A staged value outside a range that updated source narrowed is
  marked `outside range`.
- **Detach and Remove.** Both first show a confirmation with the binding, its
  owned objects and its local edits. **Cancel** changes nothing. If the model
  changes while the confirmation is open, nothing is sent. Otherwise the HUD
  prepares `detach` or `remove` and publishes it with `doc_apply` as one
  revision. Undo restores the binding.

Studio does not edit program source. New source is installed or updated through
the tool, an agent, or **New family** in Design variants.

## Persistence

Bindings are stored in the `.ark` source as `parametric_programs`: namespace,
name, the full program, its SHA-256, resolved values, owned names and baselines.
They require conditional semantic document version **55**. Documents without a
binding keep their earlier required version; removing the last binding returns
the document to its earlier version. A binding whose program declares a
boolean or `choices` parameter, or a saved design variant with a text value,
requires version **60**; documents without them keep their earlier required
version, and applications before version 60 reject such a document as a
future format instead of ignoring the new fields. A document relabelled below
60 rejects on load. Stored values must already be canonical (`1`/`0` for a
boolean, a listed choice otherwise). A tampered digest, incomplete values or
overlapping ownership reject on load without replacing the open document.
Loading and saving check each stored program's structure and expression
syntax, including the nesting limit, but not name resolution, so a document
saved before the name checks still opens and saves. Configuring such a program
or evaluating its variants rejects with the unresolved name; update it with
corrected source, remove it or detach it.

Bindings are validated in full (source, digest, values and ownership) where
they enter a document: on load, including undo, redo and iteration restores, on
`program.bind`, and again when the document is saved. Clear and detach only remove
bindings and sealing only replaces baseline digests. In-memory snapshots and
the `doc_apply` working copy reuse the validated bindings instead of
re-validating and re-hashing every program on each serialization.
Scoped design-container checkouts and component exports do not carry bindings;
the owning project keeps them. The physical container version does not change.

## Team role

`AgentTeamDiscipline::ParametricDesigner` ("Parametric designer") writes only
batches that carry a `program.*` or `variant.*` operation. `feature.add`,
`sketch.set`, `plane.set` and `material.set` are upserts, so the role may send
them only as compiled output of a `program.bind` in the same step: each name
must lie in a namespace that step binds, and `doc_apply` provenance then
requires the exact compiled commands. A `program.clear`, `program.detach` or
variant operation authorizes no other edit; for example, a detach cannot carry
a material density of another role. Steps of only `program.clear` or
`program.detach` stay allowed. `variant.set` and `variant.remove` stay allowed:
they store or remove named parameter maps of a program namespace and never
edit source objects; `variant.set` requires the installed program digest. The
team configuration does not assign namespaces to roles, so the role is not
restricted to one namespace. Its instructions load the `programs` topic, validate,
prepare in the assigned namespace, publish its own candidate with
`team_apply_step`, inspect native geometry and report namespace, values, owned
names and assertions to the orchestrator. A lead in the same team is told to
delegate counted, repeated or conditional families to it. Other writers can
also use programs; the physics reviewer can validate and inspect only.

A program candidate prepared by a team role becomes that role's retained
candidate. It still passes the role's step scope and the configured
command/feature/placement limits; Studio allows 32 features per step, so split
larger families into several namespaces.

Studio's default team is unchanged. Choose **Agents** > **Parametric designer
(optional)** to add the role in Focused or Parallel; its card has its own
Connector, Settings and Reasoning, and it starts with the `overview` and
`programs` guides. See the [team workflow](MULTI_AGENT_STUDIO.md).

## Limits

| Limit | Value |
| --- | --- |
| Source size | 256 KiB |
| Statements | 8192; blocks nest to 32 in a body |
| Evaluation steps | 50000 per run |
| Loop count | 1024 per loop or `$for` |
| Nested calls | 16; blocks of all active calls nest to 64 |
| Emitted commands | 126 per run, at most 2 MiB |
| Parameters, functions | 64 each; 16 arguments per function |
| Choices | 64 per parameter; choice text 1..64 bytes |
| `.arkprog` file | 256 KiB |
| Bindings | 64 per document |
| Expression text | 2048 bytes; nesting to 64 levels |

## Evidence and limits

The local `parametric-programs` CTest covers the language (precedence, logic,
accumulators, templates, `$for` splices and typed rejections), the expression
nesting limit on unary, power, parenthesis and call chains (also in a stored
binding on load), the deepest block/call/expression nesting the limits allow,
the block budget shared by active calls (also for tool arguments at the JSON
nesting limit), non-finite rejection in assertions, conditions, logic, loop
counts and functions, lazy `?:`/`&&`/`||` guards, static name and `set` checks in untaken
branches and functions, namespacing and
reference rewriting, install/configure/update/remove/detach candidates, stale
candidates, local edits and dependants, undo/redo, save/reopen at version 55,
downgrade and digest rejection, tool discovery and onboarding examples. It also
covers provenance: exact hand-written compiled batches bind cleanly; altered,
missing or reordered compiled commands, namespace edits before or after the
bind (by name and by numeric ID, also after or between intermediate
`assembly.drive` evaluations), merges into existing rows, a non-adjacent
clear and a double bind reject `PROGRAM_PROVENANCE`; ambiguous sketch/feature
references and JSON-escaped Skeleton mentions are rejected. The `agent-team`
CTest covers the role scope: an exact compiled step, its own prepared configure
candidate, variant-only and detach-only steps commit, while a variant or detach
carrying an outside upsert and tampered namespaced output reject. It also
covers boolean, numeric-choice and text-choice declarations, values and
`{@name}` checks; a typed binding installed, configured, saved and reopened at
version 60 (byte-identical re-save), rejected at 59 and 61, and a return to 55
after its removal; and `.arkprog` export, no-clobber and explicit overwrite,
path/format/digest/size rejections and installing from `program_path` with the
same digest, using temporary files. The `design_variants` CTest explores and
saves boolean/text values on exact boxes, and `context-graph-034d` checks the
binding node, ownership links, fingerprint changes and index invalidation. A
generated shelf is checked against an independent exact volume. The knob's
native volume matches an independent numerical integral of its authored
profile within a relative 1e-6, and a parameter change re-runs it. The local
`studio-parametric-role-ui` check publishes a two-post program from an offline
Focused team through Studio's tool queue and measures both solids natively.

The optional `ark-program-binding-cost-benchmark` target (not built by default)
binds eight generated programs of about 112 KB each and times an unrelated
`param.set` through `doc_apply` with and without the iteration journal, a
detached control, serialization, the `doc_apply` clone and save/load. Compare
executables alternately on the same machine; timings are local evidence only.

Studio `--self-test-program-hud-ui` (CTest `studio-program-hud-ui`, OCCT
builds, 1280x800 plus a 1100x700 pass) installs the built-in bookshelf through
the tool path and opens the HUD on Programs. It covers:

- the inspect listing, rows for every declared parameter, and staging that
  leaves the document unchanged;
- **+** on `shelves` and **Apply**: one revision, `shelves` 6 in the binding,
  six shelf solids, a human `doc_apply` in the journal, busy frames painted
  during the worker calls, and the sixth shelf's native volume;
- typed values rounded to an integer and clamped to `max`;
- a value typed while a configure from another writer commits during the edit:
  not staged, rows re-read at the new revision;
- `PROGRAM_ASSERT`, stale staging after an agent commit, `PROGRAM_VALUES` after
  an agent update narrowed `max`, `PROGRAM_LOCAL_EDITS` and
  `PROGRAM_DEPENDANTS`, each with the document and revision unchanged
  (`PROGRAM_STALE_PROGRAM` uses the same display path but is not produced by
  this test);
- Detach cancelled and confirmed, Remove withheld after a commit during its
  confirmation, Remove and Undo, the Variants link, and viewport bounds;
- the HUD's binding rows byte-identical to the live inspect after install,
  after a local edit, and with four imported meshes in the drawing.

Captures are written next to `--shot` (`.program-hud-open.png`, `-staged`,
`-applied`, `-rejected`, `-typed-race`, `-values`, `-local-edits`,
`-detach-dialog`, `-detached`, `-dependants`, `-remove-dialog` and `-narrow`).
In one local run with the four meshes (1.57 MB of authored source), the HUD's
inspect took 2.6 ms and the live inspect 135 ms. This is a single
measurement, not a benchmark.

Assertions and bounds encode the design rules stated by the author. They are
not manufacturing, structural or fit qualification. No measured agent quality,
time or token benefit is claimed. Editing program source in Studio is not
implemented. Studio's Design variants window stages numeric values only and the
Parametric HUD shows text values read-only, so text choices are configured
through the tools. Program assembly output is not implemented (see above).
