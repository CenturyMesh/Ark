#pragma once
#include "ark/serialize.hpp"
#include <cstdint>
#include <vector>

namespace ark {
// Recovery is a separately owned container, never the user's source file.
// Loading returns an unsaved candidate; callers must ask before applying it and
// must keep the source intact until the user explicitly saves the candidate.
struct RecoveryInfo {
    std::string sourcePath;
    std::string savedUtc;
    std::string writerToken;
    uint64_t revision=0;
};
// A process owns one Studio session. Its random writer identity prevents two
// open sessions (including untitled drawings) from replacing one candidate.
std::string recoveryWriterToken();
std::string recoveryFilePath(const std::string& directory,const std::string& sourcePath,
                             const std::string& writerToken=recoveryWriterToken());
std::vector<std::string> recoveryCandidates(const std::string& directory,const std::string& sourcePath,
                                           std::string* error=nullptr);
bool saveRecovery(const Document& document,const char* recoveryPath,const std::string& sourcePath,
                  uint64_t revision,std::string* error=nullptr);
bool saveRecovery(const Document& document,const char* recoveryPath,const std::string& sourcePath,
                  uint64_t revision,const std::string& writerToken,std::string* error=nullptr);
bool loadRecovery(const char* recoveryPath,Document& candidate,RecoveryInfo& info,std::string* error=nullptr);
} // namespace ark
