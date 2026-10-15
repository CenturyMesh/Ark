# 030D local agent, tool and viewport communication

This develops **A.04/D.02/D.04 — Parcial**. The requested pipeline means faster
communication between agents, tools and the viewport. The incremental executable
build helper is a separate development convenience.

## Ownership and transport

| Boundary | Current path | Preserved contract |
|---|---|---|
| Embedded agents to tools | Direct C++ executor calls, serialized at the shared document boundary | Focused default, optional Parallel, no added agent turn/token limits |
| Agent/MCP requests to Studio | `PostMessage` and owned `UiToolRequest` objects | UI queue, cancellation, document epoch, steering and revision guards |
| Studio to model worker | Persistent C++ worker with a completion future | One CAD owner while the UI displays a captured scene and role/draft state |
| External MCP to Studio | Local Windows named pipe, length-prefixed JSON request/reply | Existing framing, single-user ACL, size/deadline bounds, explicit unknown-outcome failures; no mutation replay |
| Published geometry to viewport | Existing scene update followed by rendering | Exact producer authority and atomic publication remain unchanged |

Internal agent calls do not encode a JSON wire packet for the UI queue. Changing
the external MCP payload format has not been shown to reduce these local costs.
No new binary wire protocol or interoperability claim is introduced here.

## Changes

Before each background model call, Studio previously rendered a stable frame,
called `glFinish`, read the whole RGB image into CPU memory and uploaded it to a
texture. The retained busy scene now uses an ordered GPU texture copy, reusing
texture storage until the window dimensions change. Actual screenshot tools
still perform their required CPU image readback.

Normal UI dispatch yields after a published revision or after eight milliseconds
of completed dispatch work, so rendering receives a turn before another batch.
One long operation is not preempted; its existing busy renderer continues to
pump permitted messages and show role markers and uncommitted draft progress.
Queued tools remain ordered and document access remains serialized. This is a
UI scheduling interval, not an agent budget.

The existing measured-activity dialog now includes the last 128 normal local
dispatches: queue, host, stable-scene capture, model worker and completion-resume
wall times. It retains tool names and times only. Host time includes its worker
and capture stages; sums of parallel role times are not elapsed run time.
These samples end before presentation and exclude provider/network time.
Cancellation and internal coordination callbacks are outside this sample list.

## Reproduction and limits

Enable `ARK_TEST_STUDIO_LIVE=ON`, build Release and run:

```powershell
ctest --test-dir out/native-release -C Release -R '^studio-realtime-ui$' --output-on-failure
```

`ark-studio --self-test-realtime-ui --settings-file <scratch.json> --shot
<capture.png> --size 1280 800` writes a timing JSON beside the capture. The
synthetic authored net is identical for each executable. Each tool has four
warmups and twenty measured requests through the actual posted UI queue,
`runTool`, background preview worker and final render with GPU completion.
`mesh_inspect` is a direct-call control; `doc_preview` uses the progress worker.

Additional checks compare every captured RGB pixel, including orientation,
exercise texture reallocation and restoration, ensure a slow two-request queue
yields to rendering without losing either callback, and retain cancellation,
stale-revision rejection and unchanged source/revision after reads/previews.
The busy screenshot contains an actual local tool operation and spatial marker.

Results in the separate realtime receipt compare the baseline and changed
executables with identical fixtures, sizes and request counts. They establish
only the measured local path. Provider inference, remote collaboration, native
geometry throughput and physical compositor/monitor presentation are separate
measurements. A warm synthetic preview improvement is not a general accepted
design speedup or reduction in model cost.
