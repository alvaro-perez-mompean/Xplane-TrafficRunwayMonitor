#include "sdk/SimbriefClient.h"

#include <cctype>
#include <mutex>

#include "core/SimbriefOfp.h"

#if IBM
#include <windows.h>
#include <wininet.h>
#endif

namespace trm::sdk {

struct SimbriefClientSharedState {
    std::mutex mutex;
    SimbriefFetchResult result;
#if IBM
    // Set by the worker thread as it opens each handle, guarded by the
    // same mutex above -- lets ~SimbriefClient() close them from the main
    // thread to abort a blocked WinINet call promptly instead of leaving
    // the worker (and the DLL it's running inside) alive past unload.
    // Both the worker and the destructor only ever close a handle after
    // confirming under this same lock that they still "own" it (i.e.
    // nobody else already closed and cleared it) -- see CloseIfOwned below
    // -- so a handle is never closed twice.
    HINTERNET hSession = nullptr;
    HINTERNET hRequest = nullptr;
#endif
};

#if IBM

namespace {

// Simbrief's userid param accepts either a numeric pilot ID or a username;
// usernames could contain characters that aren't safe unescaped in a URL.
// Minimal percent-encoding -- only what's needed for this one query value.
std::string UrlEncode(const std::string& value)
{
    static const char kHex[] = "0123456789ABCDEF";
    std::string encoded;
    for (unsigned char c : value) {
        if (std::isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
            encoded += static_cast<char>(c);
        } else {
            encoded += '%';
            encoded += kHex[c >> 4];
            encoded += kHex[c & 0x0F];
        }
    }
    return encoded;
}

// Closes `handle` and clears `*slot` iff `*slot` still equals `handle` --
// i.e. iff this call is the first to claim it. If the destructor already
// raced in and closed/cleared it, `*slot` is already nullptr and this is a
// no-op, avoiding a double InternetCloseHandle on the same handle. Must be
// called with `mutex` held.
void CloseIfOwned(HINTERNET* slot, HINTERNET handle)
{
    if (*slot == handle) {
        InternetCloseHandle(handle);
        *slot = nullptr;
    }
}

void RunFetch(std::shared_ptr<SimbriefClientSharedState> state, std::string pilotId)
{
    auto finishWithError = [&state](const std::string& message) {
        std::lock_guard<std::mutex> lock(state->mutex);
        state->result.status = SimbriefFetchStatus::kError;
        state->result.origin_icao.reset();
        state->result.destination_icao.reset();
        state->result.error_message = message;
        ++state->result.generation;
    };

    HINTERNET hSession = InternetOpenA("TrafficRunwayMonitor/1.0", INTERNET_OPEN_TYPE_PRECONFIG, nullptr, nullptr, 0);
    if (!hSession) {
        finishWithError("could not open internet session");
        return;
    }
    // Backstop against an indefinitely blocked call if the destructor's
    // close-to-abort trick can't apply yet (e.g. still resolving DNS,
    // before any handle exists for it to close) -- bounds worst-case
    // shutdown delay rather than leaving it unbounded.
    DWORD timeoutMs = 8000;
    InternetSetOptionA(hSession, INTERNET_OPTION_CONNECT_TIMEOUT, &timeoutMs, sizeof(timeoutMs));
    InternetSetOptionA(hSession, INTERNET_OPTION_SEND_TIMEOUT, &timeoutMs, sizeof(timeoutMs));
    InternetSetOptionA(hSession, INTERNET_OPTION_RECEIVE_TIMEOUT, &timeoutMs, sizeof(timeoutMs));
    {
        std::lock_guard<std::mutex> lock(state->mutex);
        state->hSession = hSession;
    }

    const std::string url = "https://www.simbrief.com/api/xml.fetcher.php?userid=" + UrlEncode(pilotId) + "&json=1";
    HINTERNET hRequest = InternetOpenUrlA(
        hSession, url.c_str(), nullptr, 0,
        INTERNET_FLAG_SECURE | INTERNET_FLAG_RELOAD | INTERNET_FLAG_NO_CACHE_WRITE, 0);
    if (!hRequest) {
        {
            std::lock_guard<std::mutex> lock(state->mutex);
            CloseIfOwned(&state->hSession, hSession);
        }
        finishWithError("could not reach Simbrief (check pilot ID and internet connection)");
        return;
    }
    {
        std::lock_guard<std::mutex> lock(state->mutex);
        state->hRequest = hRequest;
    }

    std::string body;
    char buffer[4096];
    DWORD bytesRead = 0;
    while (InternetReadFile(hRequest, buffer, sizeof(buffer), &bytesRead) && bytesRead > 0) {
        body.append(buffer, bytesRead);
    }

    {
        std::lock_guard<std::mutex> lock(state->mutex);
        CloseIfOwned(&state->hRequest, hRequest);
        CloseIfOwned(&state->hSession, hSession);
    }

    if (body.empty()) {
        finishWithError("empty response from Simbrief");
        return;
    }

    const core::SimbriefOriginDestination parsed = core::ParseSimbriefOfp(body);
    std::lock_guard<std::mutex> lock(state->mutex);
    if (!parsed.origin_icao.has_value() && !parsed.destination_icao.has_value()) {
        state->result.status = SimbriefFetchStatus::kError;
        state->result.error_message = "could not find origin/destination in Simbrief response";
    } else {
        state->result.status = SimbriefFetchStatus::kSuccess;
        state->result.error_message.clear();
    }
    state->result.origin_icao = parsed.origin_icao;
    state->result.destination_icao = parsed.destination_icao;
    ++state->result.generation;
}

} // namespace

#endif // IBM

SimbriefClient::SimbriefClient() : state_(std::make_shared<SimbriefClientSharedState>()) {}

SimbriefClient::~SimbriefClient()
{
#if IBM
    {
        std::lock_guard<std::mutex> lock(state_->mutex);
        if (state_->hRequest) {
            InternetCloseHandle(state_->hRequest);
            state_->hRequest = nullptr;
        }
        if (state_->hSession) {
            InternetCloseHandle(state_->hSession);
            state_->hSession = nullptr;
        }
    }
#endif
    if (worker_.joinable()) {
        worker_.join();
    }
}

void SimbriefClient::RequestFetch(const std::string& pilotId)
{
#if IBM
    {
        std::lock_guard<std::mutex> lock(state_->mutex);
        if (state_->result.status == SimbriefFetchStatus::kFetching) {
            return;
        }
        state_->result.status = SimbriefFetchStatus::kFetching;
        state_->result.error_message.clear();
    }
    if (worker_.joinable()) {
        // The previous fetch has already left kFetching (checked above),
        // so its thread has already finished or is finishing -- this join
        // returns promptly, not an indefinite wait.
        worker_.join();
    }
    worker_ = std::thread(&RunFetch, state_, pilotId);
#else
    (void)pilotId;
    std::lock_guard<std::mutex> lock(state_->mutex);
    state_->result.status = SimbriefFetchStatus::kError;
    state_->result.origin_icao.reset();
    state_->result.destination_icao.reset();
    state_->result.error_message = "Simbrief fetch is not supported on this platform";
    ++state_->result.generation;
#endif
}

SimbriefFetchResult SimbriefClient::Poll() const
{
    std::lock_guard<std::mutex> lock(state_->mutex);
    return state_->result;
}

} // namespace trm::sdk
