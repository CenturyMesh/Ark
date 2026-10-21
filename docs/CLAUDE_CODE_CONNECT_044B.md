# Claude Code inside ARK — 044B

This scope addresses **D.02** and **F.03**; both remain **Parcial**. A working
connector and local protocol checks do not establish CAD design quality, cost
savings or milestone completion.

Assistant and Team cards can choose **Claude Code / CLI account**. ARK starts
the Claude Code CLI that you installed, unmodified, in its headless
stream-json mode. ARK serves the current role's tools to it as an in-process
SDK MCP server and executes every tool call itself, with the same
`expected_revision`, atomic publication, steering and receipt rules as the
other connectors. The external **Live MCP** export for Claude Code remains
available when you prefer to run Claude Code yourself.

## Set up

1. Install Claude Code with Anthropic's installer or its VS Code extension.
   Run `claude` once and sign in. You can also use any other sign-in method
   that Claude Code documents, such as your own API key.
2. In ARK, open **Assistant connector** (or a Team card's **Connector**) and
   choose **Claude Code / CLI account**.
3. Leave **Claude CLI** as `claude` to detect the native executable. ARK looks
   on `PATH`, then in `%USERPROFILE%\.local\bin`, then in the newest VS Code
   extension's `resources\native-binary\claude.exe`. You can also enter the full
   path to `claude.exe`. Script shims such as `claude.cmd` are rejected, because
   ARK starts no shell.
4. Leave **Model** blank to use the CLI default, or refresh the catalog and
   choose an alias (`opus`, `sonnet`, `fable`) or a resolved model ID.
   **Effort** is blank (CLI default) or one of the levels that the selected
   model advertises. An unknown model or an unadvertised effort rejects before
   any prompt is sent.

Catalog refresh sends only the CLI's `initialize` control request. It sends no
user message and starts no inference.

## Accounts and billing

Claude Code authenticates exactly as you configured it. ARK does not read,
store or supply Claude credentials, offers no claude.ai sign-in, and leaves
every Claude Code sign-in method unchanged, including subscription sign-in, your
own API key and cloud providers. Usage is billed to your own account or plan
under your agreement with Anthropic. The CLI's `initialize` response contains
an account record. The catalog probe keeps only its API route and plan label,
and the account e-mail is discarded. ARK does not resell or intermediate
Claude usage.

Usage comes from the CLI's `result` receipts. Input tokens include cache reads
and writes; cache reads are reported as cached input, and thinking tokens as
reasoning. The CLI's cost estimate is not recorded.

## Isolation

ARK starts the CLI with these arguments:

```text
-p --input-format stream-json --output-format stream-json --verbose
--include-partial-messages --tools "" --strict-mcp-config
--mcp-config {"mcpServers":{"ark":{"type":"sdk","name":"ark"}}}
--allowedTools mcp__ark__<each role tool>
--disallowedTools Bash,PowerShell,Read,Write,Edit,NotebookEdit,Glob,Grep,WebFetch,WebSearch,Task,Agent,Skill
--permission-mode dontAsk --no-session-persistence --disable-slash-commands
--system-prompt-file <isolated role prompt> [--model M] [--effort E]
```

- The CLI process runs in a disposable working directory containing a
  `.git` fence, inside a kill-on-close job object. ARK deletes the directory
  after the run.
- Only a parent Claude Code session's markers are dropped from its
  environment: `CLAUDECODE`, `CLAUDE_CODE_ENTRYPOINT`, `CLAUDE_CODE_SSE_PORT`
  and `CLAUDE_CODE_SIMPLE`. 047D adds the session and SDK-host markers observed
  when Studio itself is started from a Claude Code IDE session:
  - `CLAUDE_PID` and `CLAUDE_AGENT_SDK_VERSION`.
  - `CLAUDE_CODE_CHILD_SESSION`, `CLAUDE_CODE_EMIT_STARTUP_TIMING`,
    `CLAUDE_CODE_ENABLE_SDK_FILE_CHECKPOINTING`, `CLAUDE_CODE_ENABLE_TASKS` and
    `CLAUDE_CODE_QUESTION_PREVIEW_FORMAT`.
  - Every `CLAUDE_CODE_SESSION_*` and `CLAUDE_CODE_MESSAGING_*` name.

  Without this, the role would inherit that session's identity, messaging
  socket and token. User settings and sign-in variables such as
  `ANTHROPIC_API_KEY` and `CLAUDE_CODE_OAUTH_TOKEN` still pass, so the sign-in
  methods configured there keep working. Hooks configured there run as they do
  in any Claude Code session.
- **stdout reader thread (047D).** A dedicated thread drains the CLI's stdout
  continuously, holding at most the configured response bound of unread bytes.
  Before 047D, one thread ran the tool and wrote its reply without reading
  stdout. A CLI that kept streaming then blocked on a full pipe and stopped
  reading stdin. ARK blocked writing a reply larger than the pipe buffer, and
  after the 90 s timeout the role ended with "stdin write was cancelled,
  disconnected or timed out".
- **Stdin write errors (047D).** These now name the cause: cancelled, timed out
  (the CLI read no input for the timeout and was stopped) or disconnected.
- **Grok Build.** The Grok Build ACP channel uses the same reader.
- Before any CAD call, ARK checks the session `init` message. The tools must be
  exactly `mcp__ark__` role tools, `ark` must be the only connected MCP server,
  and the permission mode must be `dontAsk`. Any other result stops the
  process before a tool runs.
- `can_use_tool` requests are denied. Hook callbacks and other control
  requests are rejected. Tool calls for another server, repeated control or MCP
  request IDs, and unknown tools reject without execution.

## Turns, steering and interruption

- **First prompt.** The first prompt carries the retained context and the
  current input. ARK's instructions and onboarding guide form the system
  prompt.
- **Continuation.** Within one run, later host input such as tool-feedback
  reminders, completion-check issues or steering is sent as new turns of the
  same CLI session.
- **Steering.** User steering sends an `interrupt` and then a new turn. A tool
  call that belongs to the interrupted turn is refused by the host.
- **Cancellation.** Cancellation terminates the process. Completed commits
  remain. ARK records `ARK_ACP_INSPECT_REQUIRED`, so the next run must read
  `doc_info` before writing again.
- **Sign-in failure.** A CLI that is not signed in fails with a message to run
  `claude` and complete `/login`. ARK uses no API fallback.

## Limits

- The native transport is Windows-only, like the Grok Build route.
- **User images (047D).** Inline user images (Attach image, Inspire design,
  Image to CAD and steering images) are sent as stream-json `image` blocks in
  the same user turn.
  - Only validated inline PNG or JPEG data of at most 4 MiB each is sent, up to
    20 per prompt. Nothing is fetched, dropped or recoded.
  - The JSON text keeps an `attached_image N` placeholder in place of the
    base64 data.
  - Invalid image data rejects the turn before the prompt is sent.
  - Settings saved before 047D stored `image_input_supported:false` for this
    route; Studio enables it on load.
  - Tool-result images, such as `snapshot` receipts, are not converted on this
    route.
- Each ARK run starts a fresh CLI session; earlier context is resent as
  retained text, not resumed from a stored CLI session.
- The tool list is fixed for the run. Specialist tools loaded later by
  onboarding are not re-advertised to the CLI.

## Verification

- **Offline.** `assistant-claude-code` drives a scripted stream-json peer and,
  on Windows, the real process channel. The test executable acts as a fake CLI
  that checks the arguments, the isolated directory, the role prompt and the
  inherited sign-in variables. The checks cover:
  - tool execution and receipts;
  - usage parsing;
  - model and effort negotiation;
  - catalog probing without a prompt;
  - isolation failures (an exposed built-in tool, a foreign server, repeated
    IDs, a missing catalog);
  - sign-in and turn failures;
  - permission denial, steering, cancellation and resume inspection;
  - from 047D, a user image sent as one text block plus one `image` block with
    the exact PNG data and no base64 in the text, and rejection of invalid image
    bytes;
  - from 047D, a fake CLI that writes about 1 MiB to stdout before reading a
    512 KiB tool reply, which completes. Against the 046B transport the same
    test ended with the user-reported stdin write error after its 5 s timeout.
- **Live image probe (047D).** Claude Code 2.1.304, model `haiku`. One
  stream-json user packet held a text block and a 64×64 red PNG `image` block
  with `media_type:image/png`. The CLI returned `result` `success` with the
  answer `red`. This verifies the wire format only, not image-guided CAD
  quality.
- **Live, local.** With Claude Code 2.1.301, the handshake served `initialize`
  and `tools/list` from ARK's SDK server before any prompt. One minimal live
  turn reported only `mcp__ark__doc_info` as available, `ark` connected through
  `sdk`, permission mode `dontAsk`, and no skills or slash commands. It called
  the tool through the control channel and returned a result receipt with
  usage.
- **Live CAD briefs, local.** `ark-benchmark --live` now accepts protocol
  `claude_code`. It ran two synthetic briefs through the real ARK executor with
  model alias `opus` at effort `medium`, Single strategy, one repetition each.
  Both were accepted by their native postconditions (valid, single solid, exact
  volume).

  | Brief | Task | Tool calls | Tokens (cached) | Elapsed |
  |---|---|---|---|---|
  | P01 | Parametric cylinder radius 4, height 20 | 3 | 91,613 (67,164) | 16.6 s |
  | N05 | Through cylinder cut from a 10 mm cube | 2 | 68,690 (65,154) | 15.5 s |

  Two accepted attempts do not establish a success rate, quality or cost
  comparison under D.01.

This evidence is local protocol and process verification on one machine and one
CLI version. It does not qualify other CLI versions, providers, design quality
or cost.
