// ============================================================================
//  ARK-Point :: ark/serialize.hpp
//  Binary .ark persistence and explicit JSON exports (.arkjson/.json).
//  Full fidelity: parameters,
//  named sketches (with constraints), feature history with expression
//  params, inputs, op codes and stable ids. Enables reproducible agent
//  sessions across MCP / CLI / C-ABI hosts.
// ===========================================================================
#pragma once
#include "ark/model.hpp"
#include "ark/json.hpp"

namespace ark {

// Full-document JSON (params + sketches + features; no evaluation state).
std::string documentToJson(const Document& doc,bool includeAudit=true,bool includeHistory=true);

// Replace doc contents from JSON. Returns false + err on malformed input.
bool documentFromJson(const std::string& json, Document& doc, std::string* err=nullptr);

// Binary by default; .arkjson/.json select text export. Loads detect the signature.
bool saveDocument(const Document& doc, const char* path);
bool loadDocument(const char* path, Document& doc, std::string* err=nullptr);

// Sketch <-> JSON. Accepted shape:
//   {"entities":[{"kind":"point|line|circle","dof":[..]}],
//    "constraints":[{"type":"fixed|..","a":0,"b":-1,"pa":0,"pb":0,"val":0,"val2":0}]}
std::string sketchToJson(const Sketch& sk);
bool sketchFromJson(const std::string& json, Sketch& sk, std::string* err=nullptr);

} // namespace ark
