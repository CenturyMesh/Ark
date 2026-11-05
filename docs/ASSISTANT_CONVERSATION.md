# Studio agent conversation

Milestones: **A.02 / D.02**. This is a local Studio presentation change within
the current development iteration. It does not change the document format or
the agent's permission, revision, publication or completion contracts.

The Assistant dock presents user messages and agent replies in separate native
conversation bubbles. The body and composer use 18 px Segoe UI. Paragraphs and
Unicode are retained. Each message is read-only but selectable with the mouse,
Ctrl+A and Ctrl+C. **Copy chat** copies the goal and the conversation as Unicode
text with paragraph boundaries. Copying does not submit a provider request.

The goal remains above the scrolling messages. Its height uses native text
layout, up to three lines in a wide dock and one or two in a narrow one; any goal
exceeding the available height has its own scrollbar.
**Latest** returns to the newest message. New progress follows the end only when
the reader was already there; it does not displace someone reading earlier work.
Messages are retained for the open-document session rather than discarded after
200 entries. Native message controls are created only for the visible portion
of the history. This is not persistence across an application restart.

## Right dock, width and reasoning

The Assistant is a dock on the right edge of Studio, spanning the full height
between the ribbon and the status bar. The 3D viewport ends at the dock; open
tool docks (baked motions, exploded views, inertial, rubber) sit between the
viewport and the Assistant. The header Assistant button still opens and closes
the dock. Within it:

- **Rail** (the `»` control) collapses the dock to a 44 px strip with the
  Assistant icon, a running pulse and an unread count; clicking it restores the
  dock. The conversation and composer are hidden on the rail.
- **Normal** width is 400 px at 96 dpi (scaled with the window DPI). Dragging the
  dock's left edge sets a user width; the viewport always keeps at least 460 px so
  its display bar and appearance strip stay usable.
- **Expand** / **Restore** (also the `⇔` control) switches to about 48% of the
  window, within the same viewport limit.

Open state, mode (`rail`, `normal`, `expanded`) and the user width are saved in
the Studio settings. Unknown saved values fall back to the defaults.

Agent replies carry a collapsed **Reasoning** row. It shows only text the
provider returned in readable form (see [ASSISTANT.md](ASSISTANT.md)); a reply
whose provider returned none shows the quiet note *Reasoning not exposed by this
provider*. **Show reasoning** expands every section. The reasoning row also has
one chip per agent (ARK in Single; each role in Team, with a state dot). A chip
opens that agent's latest reasoning with its role id, label, turn and state,
above the messages; extra roles are listed under **+N**. Team cards in **Agents**
have a **Reasoning** link that returns to the conversation with that role
selected. Reasoning is never added to the transcript copy, the provider history
or the document.

Local evidence for this change (milestones **A.02 / D.02**, still **Parcial**):
`studio-chat-dock-ui` renders rail, normal and expanded docks at 1280×800 and
1920×1080 with a synthetic conversation (one reply with a reasoning note, one
without) and a two-role team, and checks that the dock spans ribbon to status bar,
the viewport ends at the dock, dock controls neither escape nor overlap, the view
cube, triad and display bar stay in the viewport, the drag handle clamps, and mode
and width survive a settings round trip. Captures composite the native controls
and were inspected. Offline unit tests cover readable-reasoning extraction for
Responses, Chat Completions, Messages, Claude Code and ACP shapes and the summary
request rule. Real provider reasoning output was not exercised (no network or
credentials in this verification). With live Studio tests enabled, 78 of 81 UI
checks passed; `studio-team-blockers-ui` (and its compact variant) and
`studio-parametric-graph-ui` (stack overflow) also fail on the base revision. Self-tests
that assumed the bottom dock were updated to the right-dock layout, and the async
exact test now picks inside the current silhouette instead of an edge pixel found
before earlier orbits.

## Messages and Team controls

**Agents** opens the Team role cards in the dock column (one or more columns
by dock width, paged when they do not fit); **Chat** returns to the
conversation. Focused remains the default workflow.
**Run info** in Single exposes usage and observed activity without placing token
counters over the conversation.

Status labels distinguish:

- **Thinking**: an active model request; not a claim about private reasoning.
- **Working**: a host-observed tool operation.
- **Waiting for the connection**: an observed provider retry.
- **Waiting for your input**: Team explicitly reports `NeedsInput`, or a Single
  final reply begins with `needs_input:` and a concrete question. The display
  marker is omitted from the bubble; it grants no completion authority.
- **BOM review pending**: the Single agent repeated an unqualified final reply
  without new inspection or authoring evidence. The proposed completion is held,
  the current missing-field counts are shown, and the work is retained. This is
  a recoverable review stop, not a provider interruption or a completed goal.
- **Stopping**, **Stopped**, **Paused** and **Interrupted**: cancellation,
  pause and error states remain distinct; prior document changes are retained.
- **Reply ready**: Single has returned a reply, without asserting that the goal
  or the project is complete. Team completion is labelled **Goal reported
  complete** only when it has been explicitly declared.

The UI self-test covers the status lifecycle, native selection, paragraph and
goal copying, long replies, history beyond 200 entries, bounded native controls,
scroll retention, pinned goal layout, expanded mode and Team controls. Native
captures must be inspected as well as checking the test exit code.

057B measures response height from native character positions and font metrics,
so **Latest** reaches visible text instead of an estimated blank tail. Message
updates retain existing native controls and selection, and geometry changes
retain the reader's message anchor. Unchanged frames do not repaint the composer
or conversation header. The regression includes a long response's final line,
updates while reading/selecting, compact and wide layouts, and actual window
pixels after repeated OpenGL swaps. See [057B evidence](iterations/057B.md).

058A explicitly refreshes native screen-width wrapping before measuring text.
RichEdit can defer resize layout while Studio is minimized; using those stale
line positions created enormous blank bubbles after background messages arrived.
The regression now minimizes Studio during new messages and subsequent updates,
restores it, compares measured and cached heights, preserves selection and checks
actual final-line pixels after OpenGL swaps. Expanded conversation also suppresses
the viewport navigation/appearance controls that overlapped its Single/Team row.
See [058A evidence](iterations/058A.md).

Bare Resume/Continue in an existing Single conversation retains the displayed
goal and the host BOM task boundary. The agent receives an explicit instruction
to continue unfinished work; an explicit request to summarize remains separate.
Detailed BOM review notes stay in receipts instead of being appended in full to
every visible reply. See [the BOM recovery contract](BOM.md).
