#pragma once
#include "ark/model.hpp"
namespace ark {
// Applies authored edits only; doc_apply evaluates the entire draft atomically.
arkjson::Value applySheetCommand(Document&,const arkjson::Value&);
}
