#pragma once
#include "ark/model.hpp"
namespace ark {
// Applies authored edits only; doc_apply evaluates the entire draft atomically.
// Advanced source lists: miter, swept_flange, corner, rip, tab add/edit/remove;
// bend_table set/remove. They remain recipes in nativeSpec, never cached meshes.
arkjson::Value applySheetCommand(Document&,const arkjson::Value&);
}
