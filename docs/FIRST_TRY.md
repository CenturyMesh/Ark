# Your first ARK part

This is a short, voluntary first-use walkthrough for **F.03**. It is not a
manufacturing qualification, benchmark or invitation campaign. No account or
paid AI provider is needed to edit a part manually.

## Windows: download, edit, save and inspect

1. Download the Windows ZIP linked at the top of the [README](../README.md).
   Extract the complete archive into a writable folder and run `ARK.cmd`.
   Keep its DLLs, assets and notices together. See [updates](UPDATES.md) for the
   versioned local installation and rollback behavior.
2. Start a new drawing. Follow the [first editable plate](HANDBOOK.md) workflow:
   create an XY sketch, draw a closed rectangle and extrude it into a solid.
   Use simple dimensions such as 60 x 40 mm and a 3 mm extrusion.
3. Change one dimension, for example the width from 60 to 80 mm. Check the
   resulting solid and its editable history. Record the first confusing control
   or diagnostic; a rejected operation should preserve the prior valid document.
4. Save in your own documents folder, close that drawing and reopen the saved
   file. Verify the edit and history remain. Do not save into an immutable
   installed application-version directory.
5. Open **Manage > BOM**. Inspect the part's number, description, quantity,
   material and procurement fields. Fill a clearly labelled synthetic part
   number/description. Leave unknown sourcing facts unknown; do not invent
   supplier URLs or claim that a concept is production-ready.

The example dimensions are a learning fixture, not a useful manufactured part.
Your drawings remain yours. For AI-assisted authoring later, read
[Connecting](CONNECTING.md); connecting a model is a separate, optional step.

## Linux: headless tools

The Linux download is **not a graphical Studio**. It contains the CLI, standalone
stdio MCP server and native OCCT worker. The supplied x64 archive was tested on
Ubuntu 24.04; consult its README for required glibc/libstdc++ versions.

Extract the whole archive, then point a trusted MCP client at the absolute
`bin/ark-mcp` path. Call `agent_onboard` and `doc_info` first. Use a synthetic
document to test an edit, save and reload; standalone MCP owns its own document.
Do not expect its edits to appear in a Windows Studio window. See
[Linux build/runtime scope](NATIVE_BUILD.md) and the [CAD protocol](AGENTS.md).

## Useful feedback

Use the [first-experience form](https://github.com/CenturyMesh/Ark/issues/new?template=first_experience.yml).
Record platform/version, the task, the first blocker, whether you reached an
editable saved part, and whether save/reopen preserved the result. Optional elapsed
time is your observed time, including failed attempts; it is not a comparative
speed claim. A screenshot of the confusing step is useful if you can share it.

Do not attach credentials, customer designs or a complete account/settings
folder. Share synthetic reproductions. Security-sensitive reports go through
the [private reporting route](../SECURITY.md).
