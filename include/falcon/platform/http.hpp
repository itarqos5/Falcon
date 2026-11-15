#pragma once

#include <string>

namespace falcon {

struct HttpAnswer {
    int status = 0;  // 0: no answer at all
    std::string body;
    std::string error;
    int retryAfterSeconds = 0;  // what the server asked for with Retry-After (a 429 or 503), 0 when it did not
};

// One HTTPS request (WinHTTP: the system's certificates and proxy settings). `bearer` is sent as an access token when it is not empty, `jsonBody` as the body (with its
// content type) when it is not empty. Blocks for up to a few seconds.
[[nodiscard]] HttpAnswer httpsRequest(const std::string& method, const std::string& host, const std::string& path, const std::string& bearer = {}, const std::string& jsonBody = {});

// The same, but when the server says it is too busy (429 Too Many Requests, 502 or 503) it waits (as long as the server asks, or 1, 2, 4 seconds) and asks again, up to `maxWaitSeconds` in
// all; an answer that still says so is returned (with retryAfterSeconds). For calls on a thread that is not the screen's.
[[nodiscard]] HttpAnswer httpsRequestPatient(const std::string& method, const std::string& host, const std::string& path, const std::string& bearer = {}, const std::string& jsonBody = {},
                                             int maxWaitSeconds = 8);

// What to tell the player about a busy answer (status 429 or 5xx) from Minecraft's servers: that it is a limit, not a failure of the account, and about how long to wait.
[[nodiscard]] std::string busyAnswerWords(const HttpAnswer& answer);

}  // namespace falcon
