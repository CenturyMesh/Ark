#pragma once
#include <string>
namespace ark {
struct VerifiedNativeResultIdentity074B {
    std::string key,payloadSha256,producerSha256;
    explicit operator bool()const noexcept{return !key.empty()&&!payloadSha256.empty()&&!producerSha256.empty();}
};
// Valid only for the latest native call on this thread. Identity is granted
// after authenticating cache bytes or receiving a validated local result.
// It does not replace current-request/disk/restore validation and is not serialized.
const VerifiedNativeResultIdentity074B& lastVerifiedNativeResultIdentity074B() noexcept;
}
