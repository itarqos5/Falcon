#pragma once

// HTTP/1.1 over TLS for the systems that have no HTTPS stack of their own to call (Linux): OpenSSL for the TLS, plain sockets, nothing else. Windows uses WinHTTP instead and does not
// compile this. It is the small part that both the game (one request at a time: platform/http.cpp) and the launcher (many requests, kept connections, downloads) are built on.
//
// The server's certificate is checked against the system's bundle and its name against the host asked for.

#include <cstddef>
#include <functional>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace falcon {

class TlsStream {
public:
    TlsStream();
    ~TlsStream();
    TlsStream(const TlsStream&) = delete;
    TlsStream& operator=(const TlsStream&) = delete;

    // TCP to host:port and then TLS 1.2 or later. A wait on the network longer than `ioTimeoutMs` ends the call that waits. False, with `error` in plain words, when it fails.
    bool connect(const std::string& host, int port, int connectTimeoutMs, int ioTimeoutMs, std::string& error);
    [[nodiscard]] bool isOpen() const;
    void close();

    bool writeAll(const char* data, std::size_t size);
    // Up to `size` bytes: how many came, 0 at the end of the stream, -1 when it broke or timed out.
    int read(char* buffer, std::size_t size);
    // One line without its line end, false at the end of the stream or on an error.
    bool readLine(std::string& line);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
    std::string buffered_;  // bytes read from the network but not handed out yet
    std::size_t bufferedAt_ = 0;
};

using HttpHeaderList = std::vector<std::pair<std::string, std::string>>;

struct HttpHead {
    int status = 0;
    HttpHeaderList headers;
    std::string version;  // "HTTP/1.1"
    // The value of a header (the name in any case), empty when it is not there.
    [[nodiscard]] std::string header(const std::string& name) const;
};

// Writes "METHOD path HTTP/1.1", the headers given, Host and Content-Length, and the body. `keepAlive` false adds "Connection: close".
bool sendHttpRequest(TlsStream& stream, const std::string& method, const std::string& host, const std::string& path, const HttpHeaderList& headers, const std::string& body, bool keepAlive,
                     std::string& error);
// Reads the status line and the headers of the answer (1xx answers are skipped).
bool readHttpHead(TlsStream& stream, HttpHead& head, std::string& error);
// Reads the body that belongs to `head` (chunked, with a length, or until the connection ends) and gives it to `sink` in pieces; the sink answering false stops it (error "cancelled").
// `reusable` is set when the connection is at the end of the answer and may carry the next request.
bool readHttpBody(TlsStream& stream, const HttpHead& head, const std::function<bool(const char*, std::size_t)>& sink, bool& reusable, std::string& error);

// "https://host[:port]/path?query" taken apart; false for anything else (the launcher only follows https addresses).
struct HttpsUrl {
    std::string host;
    int port = 443;
    std::string path = "/";  // with the query
};
[[nodiscard]] bool parseHttpsUrl(const std::string& url, HttpsUrl& out);

}  // namespace falcon
