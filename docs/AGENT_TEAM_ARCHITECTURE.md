# Agent teams and module boundaries

ARK keeps engineering geometry in C++ and OCCT. Agent teams are a separate C++
orchestration module (`agent_team.hpp` / `agent_team.cpp`) over the existing
assistant connector. Manufacturing process inspection is another module over
the document and its evaluated evidence. Neither module substitutes generated
text or display triangles for an exact B-rep.

## Three different kinds of parallel work

- Independent model requests run on a bounded team worker pool. Each role has
  its own model, reasoning effort, conversation and provider usage receipts.
- Document operations share one serialized session executor. Revision checks
  prevent a proposal made against an old document from overwriting new work.
  Human steering also invalidates outstanding writes until the agent consumes
  the instruction. A kernel operation already executing must finish.
- The existing evaluator can run eligible independent feature branches on CPU
  workers. OCCT thread budgets remain bounded by `ARK_CPU_THREADS`,
  `ARK_FEATURE_THREADS` and `ARK_NATIVE_THREADS`. OpenGL and calibrated CUDA
  retain their existing display/mesh workloads; provider inference is not
  silently moved to the user's GPU.

These budgets address different workloads. Adding agents does not itself
accelerate one dependent Boolean operation, and additional model calls can
increase total tokens. Use the measured receipts and assign simpler review
work to an explicitly selected smaller model when suitable. Unknown usage
remains unknown rather than becoming zero.

## Collaboration and document authority

Mechanical engineering, design and review have separate role instructions.
The engineer is the initial document writer. Other roles inspect and send
findings/proposals to teammates. Explicit write permission is required to
change that role. The user can steer one role or the whole team and pause or
cancel subsequent work. Events describe actual requests, tool calls, messages,
errors and receipts; they do not expose hidden reasoning or invent completion
percentages.

Manufacturing intent distinguishes sheet fabrication, machining, purchased
parts, additive manufacturing and injection molding. Inspection identifies
declared process inputs, available geometry evidence and unresolved checks.
Passing an intent checklist is not physical production qualification: strength,
fatigue, material calibration, tooling and process-specific simulation still
require evidence appropriate to the part and its use.

## DLLs, workers and language choice

The new modules currently link into the existing `ark` static library. This
keeps document ownership and the tested C++ types in one application ABI.
The OCCT executable remains an isolated worker. Loading an arbitrary DLL into
the UI process would not provide that fault isolation or make its work parallel.

ARK already exposes a C ABI in `include/ark/api.h`. A future separately shipped
module can use a versioned C boundary, opaque handles and explicit allocation
ownership, or the existing structured worker protocol. A dynamic module needs
version negotiation, capability discovery and failure/lifetime tests before
being accepted; exporting STL or OCCT objects directly across independently
compiled plugin binaries would couple their compiler and library ABIs.

Rust or Zig can implement a bounded module behind such a boundary if a measured
workload justifies it. This change introduces neither language nor a speculative
GPU geometry backend. The current C++ owners already cover the new requirements
without additional language runtimes or duplicate geometry authority.
