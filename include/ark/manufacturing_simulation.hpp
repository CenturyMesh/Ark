#pragma once
#include "ark/model.hpp"
#include <functional>
namespace ark {
class AgentSession;
struct ManufacturingMove {
    Vec3 from{},to{}; double seconds=0,extrusionMm=0; int line=0,tool=0;
    bool rapid=false,cutting=false; std::string operation;
};
struct ManufacturingStudy {
    arkjson::Value receipt;
    std::vector<ManufacturingMove> moves;
    std::vector<Vec3> remainingStock;
    std::vector<Vec3> stockCells;
    std::vector<unsigned> stockRemovalMove; // 0 = retained; otherwise removal after this move
};
void validateManufacturingPlans(const Document&);
bool applyManufacturingCommand(Document&,const arkjson::Value&);
bool callManufacturingTool(AgentSession&,const std::string&,const arkjson::Value&,arkjson::Value&);
void appendManufacturingTools(arkjson::Value&);
arkjson::Value manufacturingCapabilities();
arkjson::Value manufacturingExample(const Document&,u64,const std::string& process);
arkjson::Value manufacturingGraph(const Document&,const std::string& name);
ManufacturingStudy simulateManufacturing(const Document&,u64,const std::string&,const std::function<bool()>& cancelled={});
// Reads ordinary G-code or one explicitly selected G-code member of a 3MF ZIP.
std::string readManufacturingProgram(const std::string& path,const std::string& member="");
}
