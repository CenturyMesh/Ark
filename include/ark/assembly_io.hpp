#pragma once
#include "ark/assembly.hpp"
#include "ark/json.hpp"
namespace ark {
arkjson::Value mateToJson(const AssemblyMate&);
void editMate(AssemblyMate&,const arkjson::Value&);
arkjson::Value assemblyDiagnosticsJson(const AssemblyDiagnostics&);
}
