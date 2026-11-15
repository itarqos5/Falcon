#pragma once

#include <string>

namespace falcon {

struct HttpAnswer {
    int status = 0;  // 0: no answer at all
    std::string body;
    std::string error;
};

// One HTTPS request (WinHTTP: the system's certificates and proxy settings). `bearer` is sent as an access token when it is not empty, `jsonBody` as the body (with its
// content type) when it is not empty. Blocks for up to a few seconds.
[[nodiscard]] HttpAnswer httpsRequest(const std::string& method, const std::string& host, const std::string& path, const std::string& bearer = {}, const std::string& jsonBody = {});

}  // namespace falcon
