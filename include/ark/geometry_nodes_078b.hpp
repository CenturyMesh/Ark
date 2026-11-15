#pragma once
#include "ark/model.hpp"
namespace ark {
class AgentSession;
// Domain is a connection contract, not manufacturing qualification.
std::string geometryNodeDomain078B(const Document&,const Feature&);
arkjson::Value geometryNodeCatalog078B();
arkjson::Value geometryNodeSnapshot078B(const Document&,u64 revision);
arkjson::Value geometryNodeCommands078B(const Document&,const arkjson::Value& edit);
bool callGeometryGraphTool078B(AgentSession&,const std::string&,const arkjson::Value&,arkjson::Value&);
void appendGeometryGraphTools078B(arkjson::Value&);
}
