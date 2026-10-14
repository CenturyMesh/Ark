#pragma once
#include "ark/model.hpp"

namespace ark {
// A design container is an existing feature=0 assembly group. Checkout copies
// authored source and its dependency closure into an independent document.
arkjson::Value inspectDesignContainers(const Document&,u64 revision,const arkjson::Value& args);
arkjson::Value checkoutDesignContainer(const Document&,u64 revision,const arkjson::Value& args);
// Returns a recipe for the owning session's ordinary atomic doc_apply. Does
// not change either document, their audits, or files.
arkjson::Value proposeDesignContainerMerge(const Document&,u64 revision,const arkjson::Value& args);
bool validateDesignContainerManifest(const arkjson::Value&,std::string& error);
// The host calls these before publishing authored edits/prepared candidates.
void validateDesignContainerWorkspace(const Document&);
void validateDesignContainerTransition(const Document& before,const Document& after);
}
