#pragma once

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace launcher {

using HttpHeaders = std::vector<std::pair<std::string, std::string>>;

struct HttpResponse {
    int status = 0;     // 0: no answer at all (see `error`)
    std::string body;
    std::string error;  // why there was no answer ("could not connect", "cancelled" ...)
    [[nodiscard]] bool ok() const { return status >= 200 && status < 300; }
};

// What the launcher needs from the network. An interface so the tests can answer with canned replies.
class HttpClient {
public:
    virtual ~HttpClient() = default;
    virtual HttpResponse request(const std::string& method, const std::string& url, const HttpHeaders& headers, const std::string& body) = 0;
    // Streams a GET into `to` (progress(bytes so far, total or 0); `cancel`, when set, stops it). The file is only complete when the
    // answer is ok(): nothing is left behind otherwise.
    virtual HttpResponse download(const std::string& url, const std::filesystem::path& to, const std::function<void(std::uint64_t, std::uint64_t)>& progress,
                                  const std::atomic<bool>* cancel) = 0;

    HttpResponse get(const std::string& url, const HttpHeaders& headers = {}) { return request("GET", url, headers, {}); }
    HttpResponse post(const std::string& url, const HttpHeaders& headers, const std::string& body) { return request("POST", url, headers, body); }
};

// WinHTTP: part of Windows, system proxy and certificates, TLS 1.2+, no library to ship.
[[nodiscard]] std::unique_ptr<HttpClient> makeWinHttpClient(const std::string& userAgent);

// "a=b&c=d" with the values percent-encoded.
[[nodiscard]] std::string formEncode(const std::vector<std::pair<std::string, std::string>>& fields);
[[nodiscard]] std::string urlEncode(const std::string& text);
// The reverse: %XX and "+" (as a space) are decoded; a malformed %-escape is left as it is.
[[nodiscard]] std::string urlDecode(const std::string& text);

}  // namespace launcher
