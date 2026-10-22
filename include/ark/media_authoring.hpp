#pragma once
#include "ark/model.hpp"
#include "ark/assistant.hpp"
namespace ark {
// File-producing display tools. They never publish document/geometry changes.
arkjson::Value mediaAuthoringCapabilities();
arkjson::Value generateDisplayImage(const arkjson::Value&,const AssistantTransport& offline={});
arkjson::Value composeDisplayMedia(const Document&,const arkjson::Value&);
arkjson::Value requestDisplayImage(const arkjson::Value&,const AssistantTransport& offline={},const std::atomic_bool* cancel=nullptr);
struct MediaAuthoringCancellation {
    explicit MediaAuthoringCancellation(const std::atomic_bool*);
    ~MediaAuthoringCancellation();
    MediaAuthoringCancellation(const MediaAuthoringCancellation&)=delete;
    MediaAuthoringCancellation& operator=(const MediaAuthoringCancellation&)=delete;
private: const std::atomic_bool* previous_;
};
}
