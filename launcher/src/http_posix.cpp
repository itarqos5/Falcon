// The launcher's HTTP client on Linux: HTTP/1.1 over OpenSSL (falcon/platform/tls_http.hpp), the same as the game's, with what an installation needs on top of it: connections that
// are kept for the next request (thousands of files come from two hosts), redirects (the data tables are on GitHub, which answers with an address on another host) and
// downloads that stream into a file.
#include "http.hpp"

#include "util.hpp"

#include "falcon/platform/tls_http.hpp"

#include <cstdlib>
#include <fstream>
#include <unordered_map>

namespace launcher {

namespace fs = std::filesystem;

namespace {

constexpr int kConnectTimeoutMs = 15000;
constexpr int kIoTimeoutMs = 30000;
constexpr int kMaxRedirects = 8;

using Sink = std::function<bool(const char*, std::size_t)>;

bool hasHeader(const HttpHeaders& headers, const std::string& name) {
    const std::string wanted = lowerCase(name);
    for (const auto& [key, value] : headers) {
        if (lowerCase(key) == wanted) return true;
    }
    return false;
}

class PosixHttpClient final : public HttpClient {
public:
    explicit PosixHttpClient(std::string userAgent) : userAgent_(std::move(userAgent)) {}

    HttpResponse request(const std::string& method, const std::string& url, const HttpHeaders& headers, const std::string& body) override {
        HttpResponse out;
        falcon::HttpHead head;
        const bool ok = perform(method, url, headers, body, head, nullptr, [&](const char* data, std::size_t size) {
            out.body.append(data, size);
            return true;
        }, out.error);
        if (!ok) {
            out.body.clear();
            return out;  // (status 0: no answer, and why)
        }
        out.status = head.status;
        return out;
    }

    HttpResponse download(const std::string& url, const fs::path& to, const std::function<void(std::uint64_t, std::uint64_t)>& progress, const std::atomic<bool>* cancel) override {
        HttpResponse out;
        std::error_code ec;
        fs::create_directories(to.parent_path(), ec);
        fs::path part = to;
        part += ".part";
        std::ofstream file(part, std::ios::binary | std::ios::trunc);
        if (!file) {
            out.error = "cannot write " + part.string();
            return out;
        }
        std::uint64_t total = 0, done = 0;
        bool writeFailed = false;
        falcon::HttpHead head;
        const bool ok = perform(
            "GET", url, {}, {}, head,
            [&](const falcon::HttpHead& answer) {  // only an answer that is a success is written
                if (answer.status < 200 || answer.status >= 300) return false;
                total = std::strtoull(answer.header("Content-Length").c_str(), nullptr, 10);
                return true;
            },
            [&](const char* data, std::size_t size) {
                if (cancel != nullptr && cancel->load()) return false;
                file.write(data, static_cast<std::streamsize>(size));
                if (!file) {
                    writeFailed = true;
                    return false;
                }
                done += size;
                if (progress) progress(done, total);
                return true;
            },
            out.error);
        file.close();
        if (!ok || writeFailed) {
            fs::remove(part, ec);
            out.status = 0;
            if (writeFailed) out.error = "cannot write the file";
            else if (out.error.empty()) out.error = "the download failed";
            return out;
        }
        out.status = head.status;
        if (!out.ok()) {  // (an error answer: nothing was written)
            fs::remove(part, ec);
            return out;
        }
        fs::remove(to, ec);
        fs::rename(part, to, ec);
        if (ec) {
            out.status = 0;
            out.error = "cannot move the finished download into place";
        }
        return out;
    }

private:
    using Pool = std::unordered_map<std::string, std::unique_ptr<falcon::TlsStream>>;

    // One connection per host per thread, kept for the next request.
    static Pool& pool() {
        thread_local Pool connections;
        return connections;
    }

    // The request, following redirects. `wantBody` (when given) is asked about the final answer's head first: false skips the body. False with `error` when there is no answer at all.
    bool perform(const std::string& method, const std::string& startUrl, const HttpHeaders& callerHeaders, const std::string& callerBody, falcon::HttpHead& finalHead,
                 const std::function<bool(const falcon::HttpHead&)>& wantBody, const Sink& sink, std::string& error) {
        std::string url = startUrl;
        std::string verb = method;
        std::string body = callerBody;
        for (int hop = 0; hop <= kMaxRedirects; ++hop) {
            falcon::HttpsUrl parts;
            if (!falcon::parseHttpsUrl(url, parts)) {
                error = "bad address (only https is used): " + url;
                return false;
            }
            HttpHeaders headers = callerHeaders;
            if (!hasHeader(headers, "User-Agent")) headers.emplace_back("User-Agent", userAgent_);
            if (!hasHeader(headers, "Accept")) headers.emplace_back("Accept", "*/*");
            headers.emplace_back("Accept-Encoding", "identity");  // (nothing here unpacks a compressed answer)
            const std::string key = parts.host + ":" + std::to_string(parts.port);

            falcon::HttpHead head;
            bool answered = false;
            for (int attempt = 0; attempt < 2 && !answered; ++attempt) {  // a kept connection may have been closed by the server: once more on a fresh one
                std::unique_ptr<falcon::TlsStream>& stream = pool()[key];
                const bool reused = stream != nullptr && stream->isOpen();
                if (!reused) {
                    stream = std::make_unique<falcon::TlsStream>();
                    if (!stream->connect(parts.host, parts.port, kConnectTimeoutMs, kIoTimeoutMs, error)) {
                        pool().erase(key);
                        return false;
                    }
                }
                std::string problem;
                if (falcon::sendHttpRequest(*stream, verb, parts.host, parts.path, headers, body, true, problem) && falcon::readHttpHead(*stream, head, problem)) {
                    answered = true;
                } else {
                    pool().erase(key);
                    if (!reused) {
                        error = problem;
                        return false;
                    }
                }
            }
            if (!answered) return false;

            std::unique_ptr<falcon::TlsStream>& stream = pool()[key];
            const bool redirect = head.status == 301 || head.status == 302 || head.status == 303 || head.status == 307 || head.status == 308;
            const std::string location = head.header("Location");
            bool reusable = false;
            std::string ignored;
            if (redirect && !location.empty() && hop < kMaxRedirects) {
                falcon::readHttpBody(*stream, head, [](const char*, std::size_t) { return true; }, reusable, ignored);  // (the little page that comes with a redirect)
                if (!reusable) pool().erase(key);
                if (location.compare(0, 8, "https://") == 0) url = location;
                else if (location[0] == '/') url = "https://" + parts.host + (parts.port == 443 ? "" : ":" + std::to_string(parts.port)) + location;
                else if (location.compare(0, 7, "http://") == 0) {
                    error = "redirected to an address that is not secure";
                    return false;
                } else {
                    const std::size_t slash = parts.path.rfind('/');
                    url = "https://" + parts.host + parts.path.substr(0, slash + 1) + location;
                }
                if (head.status != 307 && head.status != 308) {  // (the others turn a POST into a GET with nothing to send)
                    verb = "GET";
                    body.clear();
                }
                continue;
            }

            finalHead = head;
            if (wantBody && !wantBody(head)) {
                pool().erase(key);  // (the rest of the answer is not read: the connection cannot be used again)
                return true;
            }
            if (!falcon::readHttpBody(*stream, head, sink, reusable, error)) {
                pool().erase(key);
                return false;
            }
            if (!reusable) pool().erase(key);
            return true;
        }
        error = "too many redirects";
        return false;
    }

    std::string userAgent_;
};

}  // namespace

std::unique_ptr<HttpClient> makeSystemHttpClient(const std::string& userAgent) { return std::make_unique<PosixHttpClient>(userAgent); }

}  // namespace launcher
