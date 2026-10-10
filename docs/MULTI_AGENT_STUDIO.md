# Studio team workflow

Open **Assistant** in Studio and choose **Team**. The team shares the open
document, the selected Assistant account/endpoint and three independent
conversations:

| Role | Responsibility | Document permission |
| --- | --- | --- |
| Engineer | CAD implementation and integration | Write using observed revision |
| Designer | Design alternatives and fabrication proposals | Read and send proposals |
| Inconsistency review | Independent geometry, assembly, physics and manufacturing checks | Read and send findings |

Each card's **Settings** selects a model and reasoning effort for that role.
A blank model inherits the explicitly selected Assistant model. A blank effort
inherits Assistant effort; `auto` requests the provider default. Settings persist
locally in Studio settings and apply to a new team run. ChatGPT plan models must
be present in the connected account's loaded catalog. API model support is
validated by its provider. No implicit paid model is selected.

Send a goal with **Start** or Ctrl+Enter. Cards show actual runtime state,
provider requests, tool activity, role token totals and the latest messages.
Designer and reviewer communicate proposals to the engineer with `team_send`;
all roles can inspect `team_context`. The event log includes actual tool starts,
finishes, failures and messages. **Idle** means that role's current turn ended;
it is not evidence that the product is complete or manufacturing qualified.

While the team is running, enter another decision and choose **Queue**.
**To: entire team** can target one role instead. An active conversation receives
the decision before its next provider request. An idle recipient starts a new
turn. Requests have bounded conversation, event, mailbox and turn limits; a
rejected decision is reported explicitly. Late decisions invalidate writes from
an already in-flight response until the new input is consumed.

**Pause** gates subsequent provider requests and tools. It does not interrupt
an HTTP request or geometry operation already executing. **Cancel** requests
cancellation; already applied document changes remain. Finish or cancel active
work before changing mode or role settings. Switching to **Single** closes the
idle team. Changing the open document or connector invalidates the old team.

The runtime executes provider conversations concurrently. Studio marshals every
CAD call through its existing owning-thread queue, and the runtime serializes
the shared executor. Team writes must supply the model's observed
`expected_revision`; stale or missing revisions reject without automatic
rebasing. The single Assistant and team cannot run simultaneously.

Tokens come only from provider-reported usage. Each role has a separate
`studio_team_<role>` receipt in the document usage ledger; repeated snapshots update
the same receipt rather than counting it twice. Missing usage remains unknown
and makes totals a reported lower bound. Counts do not estimate price or cover
unreported external MCP consumption. Team usage is recorded only while the UI
owns the document, never while a geometry worker owns the session.

During an active CAD or catalog operation, the existing Studio owner overlay
keeps rendering while its worker exclusively owns the session. Input waits until
that operation returns. Team decisions and pause/cancel are cooperative at the
document, tool and provider boundaries; they do not interrupt executing geometry.

## Discover every tool

Every module ribbon contains **All tools**. This opens a categorized gallery
with native live search, scrolling and direct specialist dialogs. The gallery
scrolls its category list separately when the available screen height is small.
The gallery and command palette use the same catalog, including operations such as
editing/removing sheet flanges, hems and stamps, native surface tools, frames,
electronics, motion relations, assembly controls and team settings. Search spans
all categories, even when one category is selected.

**Advanced CAD API** lists every tool advertised by the current kernel schema.
It shows the tool description and opens a JSON argument editor seeded from
required schema fields, including the current revision where applicable. It
does not invent meaningful geometry arguments: users supply the requested
identifiers, commands, dimensions or paths before execution. Actual structured
results appear in a result dialog. This provides access to inspections and
specialist operations that do not yet have dedicated forms.

## Reuse components

**Library** appears on the Model and Tools ribbons and in the **Components &
library** gallery category. Search the persistent local library by text,
category, declared standard, thread diameter/pitch, length or material. Choose
a result to inspect its recorded source, license, declared metadata and native
geometry evidence, or insert it into the current document using a placement
form. The current revision guards insertion. Additional instances reuse the
existing component definition, and existing visible parts remain represented.

**Import STEP into component library** copies a selected file into immutable
library storage and verifies its native geometry. Its form records component
name, category, known provenance, source URL, license, material and standard.
Blank optional fields remain unknown. Select `auto` only for a single-solid
source, `all` for an intentional compound, or an explicit zero-based body index.
Ingest does not replace the open document.

The default library location follows `ARK_COMPONENT_LIBRARY` or the local ARK
application-data folder. A directory can be selected in the forms. Component
source registry, public catalog search and bounded STEP download operations are
also discoverable in Advanced CAD API. Source/license metadata, standards and
nominal dimensions are declarations; stored B-rep validity is separate evidence.

Offline verification:

```powershell
out/native-release/Release/ark-studio.exe --self-test-assistant-ui --shot out/team-ui-smoke.png --settings-file out/team-ui-settings.json
```

The selftest uses three injectable local transports without provider calls. It
checks serialized edits, reviewer write rejection, role models/efforts, token
receipts, dynamic decisions, pause/resume/cancel and distinct toolbar icons.
It also verifies gallery coverage, native filtering, paging, routed specialist
dialogs, full API coverage and local STEP library ingest/search/insert/reuse.
The test uses an isolated library beside its output path. It writes `.team.png`,
`.sketch-icons.png`, `.tool-gallery.png`, `.tool-search.png` and
`.tool-gallery-laptop.png` at 1280 by 720, plus
`.library-search.png` screenshots beside the main shot. The team screenshot is
explicitly labeled as an offline test.
