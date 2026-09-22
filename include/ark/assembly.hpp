#pragma once
#include "ark/core.hpp"
#include <unordered_map>
namespace ark {
struct AssemblyNode {
    u64 id=0,parent=0,feature=0; // feature=0 is a subassembly/group
    std::string name;
    std::unordered_map<std::string,std::string> p; // local rigid transform expressions
    bool visible=true;
};
struct AssemblyPlacement {u64 node=0,feature=0;std::string name;Mat4 world;bool visible=true;};
class Document;
bool resolveAssembly(const Document&,std::vector<AssemblyPlacement>&,std::string& error);
}
