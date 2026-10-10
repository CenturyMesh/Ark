#pragma once
#include <string>

namespace ark {
// Product version supplied by CMake, independent of document schema versions.
const char* app_version() noexcept;
// At the start of WinMain, redirects an old shortcut to the verified selected runtime.
// Returns true only after handing off; caller then exits before creating a document.
bool redirect_to_updated_runtime() noexcept;
// Opens the bundled Windows update manager. The running document remains open.
bool open_update_manager(std::string& error);
// Call after successful Studio initialization; confirms a staged launch.
void acknowledge_update_startup() noexcept;
}
