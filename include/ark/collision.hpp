#pragma once
#include "ark/model.hpp"
namespace ark {
// Read-only explicit body/occurrence selection. Native solid queries use the
// exact worker; mesh queries disclose triangle authority and containment limits.
arkjson::Value inspectDocumentCollision(Document&, const arkjson::Value& args);
}
