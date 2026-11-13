# Claude Code reconnection — 071D

**A.02 / D.02 / D.04 — Parcial**. ARK recognizes an observed Claude Code
authentication failure and provides a human account action. Geometry, conversation
receipts and pending work remain under their existing revision contracts.

Open the **Claude Code** account menu and choose **Reconnect Claude Code...**.
The same menu is available from **All tools → Assistant account / reconnect**.
ARK opens the configured native Claude CLI's official sign-in console/browser.
Complete sign-in there. The official CLI owns the credentials; ARK has no token
entry field and does not read or copy its credential store. If several configured
Claude endpoints exist, choose the intended assistant or team route first.

The action runs `claude auth login`, then checks `claude auth status` and the
existing SDK initialization metadata. It sends no model prompt. Account and SDK
metadata being available does not prove paid model access or completion of the
CAD task. ARK reports the result and waits for an explicit manual continuation.
It does not replay tools, clear the conversation or switch the selected model.
See the official [authentication guide](https://code.claude.com/docs/en/authentication)
and [CLI reference](https://code.claude.com/docs/en/cli-reference).

**Check Claude Code account (no inference)** performs the metadata checks without
opening sign-in. This check cannot clear a previously observed expired-token
warning. A successful fresh sign-in clears that warning only when the active
route is the saved `claude.ai` login and no external credential override is set.
A subsequent successful model response with reported output also clears the
historical warning, including after the user renewed the login outside ARK.
Earlier error records stay in the conversation.

An explicitly configured `CLAUDE_CODE_OAUTH_TOKEN`, `ANTHROPIC_API_KEY` or
`ANTHROPIC_AUTH_TOKEN` is preserved. Browser login does not replace those values.
ARK checks only their presence and reports that the credential owner must renew
the active override. API-key/helper and third-party routes are likewise not
reported as repaired subscription logins.

Reconnection is deferred while an assistant/team request or another account
operation runs. Starting or resuming a task is blocked during the account action;
the unsent prompt is retained. Cancellation and application shutdown stop only
the authentication process owned by ARK. They do not close the browser, another
Claude session or a CAD document.

The native helper uses a disposable empty project directory and the connector's
existing parent-marker environment filter. It preserves the CLI's configured
authentication directory, routing and credentials. The hidden status command
has bounded output and a timeout; raw account output and native stderr are not
copied into ARK's UI, project memory, graph or provider history.

The offline regression fixtures cover process arguments, metadata validation,
credential overrides, cancellation, native child environment filtering and the
human menu/expiry states. They use owned synthetic processes and callbacks, with
no real sign-in or inference. Final iteration verification records the executed
test and screenshot evidence separately.

Local verification passed **25 native authentication checks** and both normal
and compact conversation UI suites. The UI fixtures exercise account-menu
discovery, captured-route error attribution, metadata-only versus genuine-response
recovery, blocked task starts and preservation of document revision/history.
The expired and ready conversation captures were inspected for readable wrapping.
The actual native popup and complete application were also inspected at normal
and compact sizes. Reconnect/check entries are disabled during account work,
while cancellation remains available. Capture validation reads the application's
GL backbuffer and the real native controls; popup rendering must pass painted-area
and interior text-edge checks before an image is accepted.
These fixtures do not claim a new end-to-end browser sign-in through the product
button; they deliberately avoid changing a real account during regression tests.
