#pragma once
#include "ark/assistant.hpp"
#include <string>
namespace ark {
// Per-user provider API keys entered in Studio. Each key is bound to the
// connector's provider and endpoint host and protected with Windows DPAPI for
// the current user; it is never written to settings, documents, logs or memory.
// ARK_API_KEY_DIRECTORY overrides the storage directory (tests, portable runs).
bool assistantApiKeyStoreSupported();
// Throws on an invalid key, a non-HTTPS endpoint or a storage failure.
void storeAssistantApiKey(const AssistantConfig& config,const std::string& key);
bool assistantApiKeyStored(const AssistantConfig& config);
// Returns true when a stored key was removed.
bool removeAssistantApiKey(const AssistantConfig& config);
// Empty when no key is stored for this provider and host. Callers wipe the value.
std::string loadAssistantApiKey(const AssistantConfig& config);
}
