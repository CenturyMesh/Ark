# Linked geometry authoring

Use `agent_onboard` topic `geometry_nodes`, then `geometry_graph` catalog and
selected inspection. The graph represents existing editable feature recipes and
ordered inputs. Meshes, exact surfaces and solids have distinct socket domains.
Only an explicit sampling node uses display tessellation to produce a mesh;
it never promotes sampled geometry into production B-rep authority.

Prepare a change at the observed revision, inspect its candidate geometry and
publish `candidate_id` through `doc_apply` at that same revision. A preview or
drawn connector is not a committed design. Invalid cycles, missing sources,
incomplete required inputs and incompatible direct links reject atomically.

The human canvas reviews a checked candidate before Apply. Existing-target
comparisons render current and proposed geometry with one camera and scale.
Cancel releases the retained proposal; stale Apply rejects. Preview images
remain derived display evidence, and Apply uses the existing checked candidate
instead of repeating its geometry calculation.

Inspection is bounded. Follow `next_call` for remaining geometry-node pages;
use `context_call` with `context_graph` to discover adjacent sources/consumers.
Read `object_context` for selected authoring and native references before editing.
Do not treat a domain label as global geometric validity or manufacturing proof.

See [[ARK Knowledge/Workflow]] for context freshness and publication rules.
