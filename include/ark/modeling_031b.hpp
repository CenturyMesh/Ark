#pragma once
#include "ark/model.hpp"
namespace ark {
bool modeling031BNeedsVersion43(const Document& document);
void enrichModeling031BSchemas(arkjson::Value& tools);
void appendModeling031BOnboarding(arkjson::Value& output,const std::string& topic);
arkjson::Value resolveCableBindings031B(const Feature&,const ExprEnv&,const Document&,
                                      const std::vector<AssemblyPlacement>* pose=nullptr);
arkjson::Value inspectAssemblySweep031B(const Document&,const arkjson::Value& args);
}
