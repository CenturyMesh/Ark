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
void sealParametricProgramBindings(Document&);
bool callParametricProgramTool(AgentSession&,const std::string&,const arkjson::Value&,arkjson::Value&);
void appendParametricProgramTools(arkjson::Value&);
arkjson::Value parametricProgramExamples();
}
