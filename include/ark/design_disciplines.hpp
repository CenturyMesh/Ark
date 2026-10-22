#pragma once
#include "ark/model.hpp"
#include <set>
namespace ark {
class AgentSession;
arkjson::Value validateClassification(const arkjson::Value&);
std::set<std::string> effectiveFamilies(const Document&,u64 feature,u64 node=0);
bool matchesFamilyFilter(const Document&,u64 feature,u64 node,const std::set<std::string>&);
void validateDesignDisciplines(const Document&);
bool requiresDesignDisciplines054B(const Document&);
bool applyDesignDisciplineCommand(Document&,const arkjson::Value&);
const arkjson::Value* findSchematic(const Document&,const std::string&);
arkjson::Value inspectSchematic(const Document&,u64,const std::string&);
arkjson::Value inspectClassifications(const Document&,u64);
struct SchematicLine {Vec2 a,b;std::string layer;};
struct SchematicLabel {Vec2 point;std::string text;};
struct SchematicPin {Vec2 point;std::string symbol,pin;};
struct SchematicDrawing {std::vector<SchematicLine> lines;std::vector<SchematicLabel> labels;std::vector<SchematicPin> pins;};
SchematicDrawing schematicDrawing(const Document&,const arkjson::Value& sheet);
bool callDesignDisciplineTool(AgentSession&,const std::string&,const arkjson::Value&,arkjson::Value&);
void appendDesignDisciplineTools(arkjson::Value&);
}
