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
layout; goals longer than three lines have their own scrollbar. **Expand** gives
the conversation the workspace, and **Restore** returns to the CAD split.
**Latest** returns to the newest message. New progress follows the end only when
the reader was already there; it does not displace someone reading earlier work.
Messages are retained for the open-document session rather than discarded after
200 entries. Native message controls are created only for the visible portion
of the history. This is not persistence across an application restart.

**Agents** opens the existing Team role controls in the expanded workspace;
**Chat** returns to the conversation. Focused remains the default workflow.
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

Bare Resume/Continue in an existing Single conversation retains the displayed
goal and the host BOM task boundary. The agent receives an explicit instruction
to continue unfinished work; an explicit request to summarize remains separate.
Detailed BOM review notes stay in receipts instead of being appended in full to
every visible reply. See [the BOM recovery contract](BOM.md).
