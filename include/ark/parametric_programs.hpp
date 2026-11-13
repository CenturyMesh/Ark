#pragma once
#include "ark/agent.hpp"

namespace ark {
// ark.parametric-program.v1: bounded deterministic design source. A program
// compiles parameter values into ordinary native authoring commands under one
// namespace. The exact evaluator, expected_revision and atomic publication stay
// authoritative; the program never produces geometry itself.
arkjson::Value compileParametricProgram(const arkjson::Value& program,const arkjson::Value& values,const std::string& nameSpace);
std::string parametricProgramHash(const arkjson::Value& program);
void validateParametricPrograms(const arkjson::Value& entries);
bool applyParametricProgramCommand(Document&,const arkjson::Value& command);
// doc_apply provenance (PROGRAM_PROVENANCE): every program.bind in a batch is
// immediately preceded by exactly its compiled commands, optionally after one
// program.clear of its namespace, and no other command of the batch changes
// that namespace. index receives the offending command on rejection.
void verifyParametricProgramBatch(const Document& before,const arkjson::Value& commands,size_t& index);
// Before each evaluation of the batch reseals them, rows owned by bindings made
// in this batch must still be exactly what program.bind fingerprinted or the
// previous evaluation of the batch sealed.
void verifyParametricProgramSeal(const Document&);
// Reseals baselines of this batch's bindings; doc_apply clears the transient
// programBindingsToSeal list only when it publishes the batch.
void sealParametricProgramBindings(Document&);
// Boolean, choice and text-choice parameters in a binding, or a text value in
// a saved variant, require conditional semantic document version 60.
bool parametricProgramsRequire60(const Document&);
bool callParametricProgramTool(AgentSession&,const std::string&,const arkjson::Value&,arkjson::Value&);
void appendParametricProgramTools(arkjson::Value&);
arkjson::Value parametricProgramExamples();
}
