#pragma once
#include "ark/json.hpp"
#include <atomic>
#include <cstdint>
#include <string>
#include <vector>

namespace ark {
inline constexpr std::size_t chatGptRequestByteLimit=4*1024*1024; // Same serialized-body bound in preflight and HTTPS.


struct ChatGptAccountSummary {
    std::string clientId,subject,email;
    bool selected=false,authorized=false;
    std::int64_t accessExpiresAt=0;
};
struct ChatGptAuthStatus {
    bool available=false,connected=false,planAuthorized=false;
    bool remoteRevocationConfirmed=true;
    std::string activeClientId,activeEmail;
    std::string pendingClientId; // issued registration retained for invalid_grant retry
    std::string message;
    std::vector<ChatGptAccountSummary> accounts;
};
struct ChatGptModel {
    std::string slug,displayName;
    std::vector<std::string> reasoningEfforts; // empty unless explicit in account catalog
    std::string defaultReasoningEffort;        // empty means service default unknown
};
struct ChatGptIdentity {std::string issuer,clientId,subject,email;std::int64_t expiresAt=0;};
struct ChatGptAuthAttempt {
    std::string hostId,clientId,state,nonce,codeVerifier,codeChallenge,redirectUri,authorizationUrl;
    std::string expectedSubject;bool newRegistration=true;
};
struct ChatGptCallback {std::string code,issuedClientId;};

// Safe, offline status. No token or authorization URL is returned.
ChatGptAuthStatus chatGptAuthStatus();
// Blocking system-browser + 127.0.0.1 OAuth/PKCE. Call on a worker thread.
// Empty selectedClientId registers a new ChatGPT account for this host.
ChatGptAuthStatus chatGptSignIn(const std::string& selectedClientId={},const std::atomic_bool* cancel=nullptr,bool requestPlanConsent=false);
// Select a saved registration (its credentials remain isolated from other IDs).
ChatGptAuthStatus chatGptSelectAccount(const std::string& issuedClientId);
// Revokes the selected renewable session when possible, then clears local tokens.
ChatGptAuthStatus chatGptSignOut(const std::atomic_bool* cancel=nullptr);
// Fetches account-scoped visible models, preserving the service's ordering.
std::vector<ChatGptModel> chatGptListModels(const std::atomic_bool* cancel=nullptr);
// Pure parser for the account-scoped ChatGPT catalog; no network or credentials.
std::vector<ChatGptModel> parseChatGptModelCatalog(const arkjson::Value& catalogue);

// Used only by the assistant's strict ChatGPT-plan provider path.
bool chatGptPlanReady();
arkjson::Value chatGptResponses(const arkjson::Value& request,const std::atomic_bool* cancel=nullptr);

// Pure protocol seams for deterministic offline tests; do not expose secrets
// from these return values to UI, logs, tool outputs, or model conversations.
ChatGptAuthAttempt makeChatGptAuthAttempt(const std::string& hostId,const std::string& redirectUri,
                                          const std::string& issuedClientId,const std::string& expectedSubject,
                                          const std::string& idTokenHint,const std::string& loginHint,
                                          const std::string& state,const std::string& nonce,const std::string& verifier,
                                          bool requestPlanConsent=false);
ChatGptCallback parseChatGptCallback(const std::string& requestTarget,const ChatGptAuthAttempt& attempt);
ChatGptIdentity verifyChatGptIdToken(const std::string& jwt,const arkjson::Value& jwks,
                                     const std::string& issuedClientId,const std::string& nonce,std::int64_t nowUnix);
arkjson::Value parseChatGptSse(const std::string& sse);
// The same bounded HTTP decoder used by chatGptResponses, without account or
// network access. Non-200 responses throw sanitized AssistantHttpError;
// successful HTTP streams keep their existing terminal-response validation.
arkjson::Value parseChatGptResponsesHttp(unsigned status,const std::string& body,
    const std::string& retryAfter,const std::string& requestId,std::int64_t nowUnix);
// Offline DPAPI fixture seam. The path must resolve inside the OS temp folder;
// this never reads, replaces or deletes the real ARK account store.
bool chatGptDpapiRoundTripForTest(const std::string& absoluteTempFile);

} // namespace ark
