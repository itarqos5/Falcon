#include "falcon/platform/tls_http.hpp"

#include <openssl/err.h>
#include <openssl/ssl.h>
#include <openssl/x509v3.h>

#include <arpa/inet.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

#include <algorithm>
#include <cctype>
#include <cerrno>
#include <csignal>
#include <cstdint>
#include <cstdlib>
#include <mutex>

namespace falcon {

namespace {

std::string sslProblem() {
    const unsigned long code = ERR_get_error();
    if (code == 0) return "the connection failed";
    char text[256];
    ERR_error_string_n(code, text, sizeof text);
    return text;
}

std::string lowerAscii(std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

// One context for all connections: the settings, and the certificates read once.
SSL_CTX* sharedContext() {
    static SSL_CTX* context = [] {
        SSL_CTX* made = SSL_CTX_new(TLS_client_method());
        if (made == nullptr) return made;
        SSL_CTX_set_min_proto_version(made, TLS1_2_VERSION);
        SSL_CTX_set_verify(made, SSL_VERIFY_PEER, nullptr);
        SSL_CTX_set_mode(made, SSL_MODE_ENABLE_PARTIAL_WRITE | SSL_MODE_AUTO_RETRY);
#ifdef SSL_OP_IGNORE_UNEXPECTED_EOF
        SSL_CTX_set_options(made, SSL_OP_IGNORE_UNEXPECTED_EOF);  // (a server that closes the connection without the TLS goodbye has ended its answer; lengths are checked by the HTTP layer)
#endif
        // SSL_CERT_FILE / SSL_CERT_DIR, and the place this OpenSSL was built to look in...
        SSL_CTX_set_default_verify_paths(made);
        // ...and the places the distributions put their bundle, which an OpenSSL linked into the program does not know about.
        for (const char* bundle : {"/etc/ssl/certs/ca-certificates.crt", "/etc/pki/tls/certs/ca-bundle.crt", "/etc/ssl/ca-bundle.pem", "/etc/pki/tls/cacert.pem", "/etc/ssl/cert.pem"}) {
            if (access(bundle, R_OK) == 0 && SSL_CTX_load_verify_locations(made, bundle, nullptr) == 1) break;
        }
        return made;
    }();
    return context;
}

// A TCP connection to host:port, or -1 (with `error`).
int connectSocket(const std::string& host, int port, int connectTimeoutMs, std::string& error) {
    addrinfo hints{};
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    addrinfo* found = nullptr;
    if (getaddrinfo(host.c_str(), std::to_string(port).c_str(), &hints, &found) != 0 || found == nullptr) {
        error = "no internet connection (could not find " + host + ")";
        return -1;
    }
    int descriptor = -1;
    for (addrinfo* a = found; a != nullptr && descriptor < 0; a = a->ai_next) {
        int type = a->ai_socktype;
#ifdef SOCK_CLOEXEC
        type |= SOCK_CLOEXEC;  // (macOS has none: the flag is set on the descriptor below instead)
#endif
        const int candidate = ::socket(a->ai_family, type, a->ai_protocol);
        if (candidate < 0) continue;
#ifndef SOCK_CLOEXEC
        fcntl(candidate, F_SETFD, fcntl(candidate, F_GETFD, 0) | FD_CLOEXEC);
#endif
        const int flags = fcntl(candidate, F_GETFL, 0);
        fcntl(candidate, F_SETFL, flags | O_NONBLOCK);  // (to be able to stop waiting for an address that does not answer)
        int result = ::connect(candidate, a->ai_addr, a->ai_addrlen);
        if (result != 0 && errno == EINPROGRESS) {
            pollfd waiting{candidate, POLLOUT, 0};
            int problem = 0;
            socklen_t size = sizeof problem;
            result = poll(&waiting, 1, connectTimeoutMs) == 1 && getsockopt(candidate, SOL_SOCKET, SO_ERROR, &problem, &size) == 0 && problem == 0 ? 0 : -1;
        }
        if (result == 0) {
            fcntl(candidate, F_SETFL, flags);
            descriptor = candidate;
        } else {
            ::close(candidate);
        }
    }
    freeaddrinfo(found);
    if (descriptor < 0) error = "could not connect to " + host;
    return descriptor;
}

}  // namespace

struct TlsStream::Impl {
    int descriptor = -1;
    SSL* ssl = nullptr;
};

TlsStream::TlsStream() : impl_(std::make_unique<Impl>()) {}

TlsStream::~TlsStream() { close(); }

bool TlsStream::isOpen() const { return impl_->ssl != nullptr; }

void TlsStream::close() {
    if (impl_->ssl != nullptr) {
        SSL_shutdown(impl_->ssl);
        SSL_free(impl_->ssl);
        impl_->ssl = nullptr;
    }
    if (impl_->descriptor >= 0) {
        ::close(impl_->descriptor);
        impl_->descriptor = -1;
    }
    buffered_.clear();
    bufferedAt_ = 0;
}

bool TlsStream::connect(const std::string& host, int port, int connectTimeoutMs, int ioTimeoutMs, std::string& error) {
    close();
    static std::once_flag once;
    std::call_once(once, [] { std::signal(SIGPIPE, SIG_IGN); });  // (a server that hangs up must not end the whole program from inside a write)

    impl_->descriptor = connectSocket(host, port, connectTimeoutMs, error);
    if (impl_->descriptor < 0) return false;
    const int one = 1;
    setsockopt(impl_->descriptor, IPPROTO_TCP, TCP_NODELAY, &one, sizeof one);
    timeval limit{ioTimeoutMs / 1000, (ioTimeoutMs % 1000) * 1000};
    setsockopt(impl_->descriptor, SOL_SOCKET, SO_RCVTIMEO, &limit, sizeof limit);
    setsockopt(impl_->descriptor, SOL_SOCKET, SO_SNDTIMEO, &limit, sizeof limit);

    SSL_CTX* context = sharedContext();
    impl_->ssl = context != nullptr ? SSL_new(context) : nullptr;
    if (impl_->ssl == nullptr || SSL_set_fd(impl_->ssl, impl_->descriptor) != 1 || SSL_set_tlsext_host_name(impl_->ssl, host.c_str()) != 1 || SSL_set1_host(impl_->ssl, host.c_str()) != 1) {
        error = "the secure connection cannot start";
        close();
        return false;
    }
    if (SSL_connect(impl_->ssl) != 1) {
        const long verify = SSL_get_verify_result(impl_->ssl);
        error = verify != X509_V_OK ? "the certificate of " + host + " is not accepted (" + X509_verify_cert_error_string(verify) + "; check the PC's date and time)"
                                    : "the secure connection to " + host + " failed (" + sslProblem() + ")";
        close();
        return false;
    }
    return true;
}

bool TlsStream::writeAll(const char* data, std::size_t size) {
    if (impl_->ssl == nullptr) return false;
    std::size_t sent = 0;
    while (sent < size) {
        const int written = SSL_write(impl_->ssl, data + sent, static_cast<int>(std::min<std::size_t>(size - sent, 1u << 20)));
        if (written <= 0) return false;
        sent += static_cast<std::size_t>(written);
    }
    return true;
}

int TlsStream::read(char* buffer, std::size_t size) {
    if (size == 0) return 0;
    if (bufferedAt_ < buffered_.size()) {
        const std::size_t n = std::min(size, buffered_.size() - bufferedAt_);
        buffered_.copy(buffer, n, bufferedAt_);
        bufferedAt_ += n;
        if (bufferedAt_ == buffered_.size()) {
            buffered_.clear();
            bufferedAt_ = 0;
        }
        return static_cast<int>(n);
    }
    if (impl_->ssl == nullptr) return -1;
    const int got = SSL_read(impl_->ssl, buffer, static_cast<int>(std::min<std::size_t>(size, 1u << 20)));
    if (got > 0) return got;
    const int why = SSL_get_error(impl_->ssl, got);
    return why == SSL_ERROR_ZERO_RETURN ? 0 : (why == SSL_ERROR_SYSCALL && got == 0 ? 0 : -1);  // (a server that just closes without the TLS goodbye is an end too)
}

bool TlsStream::readLine(std::string& line) {
    line.clear();
    for (;;) {
        for (; bufferedAt_ < buffered_.size(); ++bufferedAt_) {
            const char c = buffered_[bufferedAt_];
            if (c == '\n') {
                ++bufferedAt_;
                if (!line.empty() && line.back() == '\r') line.pop_back();
                return true;
            }
            line.push_back(c);
            if (line.size() > 65536) return false;  // (not a header line any more)
        }
        buffered_.clear();
        bufferedAt_ = 0;
        char chunk[4096];
        if (impl_->ssl == nullptr) return false;
        const int got = SSL_read(impl_->ssl, chunk, sizeof chunk);
        if (got <= 0) return false;
        buffered_.assign(chunk, static_cast<std::size_t>(got));
    }
}

std::string HttpHead::header(const std::string& name) const {
    const std::string wanted = lowerAscii(name);
    for (const auto& [key, value] : headers) {
        if (lowerAscii(key) == wanted) return value;
    }
    return {};
}

bool sendHttpRequest(TlsStream& stream, const std::string& method, const std::string& host, const std::string& path, const HttpHeaderList& headers, const std::string& body, bool keepAlive,
                     std::string& error) {
    std::string request = method + " " + path + " HTTP/1.1\r\nHost: " + host + "\r\n";
    for (const auto& [key, value] : headers) request += key + ": " + value + "\r\n";
    if (!body.empty() || method == "POST" || method == "PUT") request += "Content-Length: " + std::to_string(body.size()) + "\r\n";
    if (!keepAlive) request += "Connection: close\r\n";
    request += "\r\n";
    if (!stream.writeAll(request.data(), request.size()) || (!body.empty() && !stream.writeAll(body.data(), body.size()))) {
        error = "the request could not be sent";
        return false;
    }
    return true;
}

bool readHttpHead(TlsStream& stream, HttpHead& head, std::string& error) {
    for (;;) {  // (an interim 1xx answer is followed by the real one)
        head = HttpHead{};
        std::string line;
        if (!stream.readLine(line)) {
            error = "the server closed the connection without an answer";
            return false;
        }
        if (line.compare(0, 5, "HTTP/") != 0) {
            error = "an answer that is not HTTP";
            return false;
        }
        const std::size_t space = line.find(' ');
        head.version = line.substr(0, space);
        head.status = space == std::string::npos ? 0 : std::atoi(line.c_str() + space + 1);
        while (stream.readLine(line) && !line.empty()) {
            const std::size_t colon = line.find(':');
            if (colon == std::string::npos) continue;
            std::size_t from = colon + 1;
            while (from < line.size() && (line[from] == ' ' || line[from] == '\t')) ++from;
            head.headers.emplace_back(line.substr(0, colon), line.substr(from));
        }
        if (head.status >= 200 || head.status == 101) return true;
        if (head.status < 100) {
            error = "an answer that is not HTTP";
            return false;
        }
    }
}

bool readHttpBody(TlsStream& stream, const HttpHead& head, const std::function<bool(const char*, std::size_t)>& sink, bool& reusable, std::string& error) {
    reusable = false;
    const std::string connection = lowerAscii(head.header("Connection"));
    const bool keepAlive = head.version != "HTTP/1.0" && connection.find("close") == std::string::npos;
    char buffer[16384];
    const auto stopped = [&] {
        error = "cancelled";
        return false;
    };
    const auto readExactly = [&](std::uint64_t count) {  // false: cut short (error set) or stopped by the sink (error "cancelled")
        while (count > 0) {
            const int got = stream.read(buffer, static_cast<std::size_t>(std::min<std::uint64_t>(count, sizeof buffer)));
            if (got <= 0) {
                error = "the connection broke while reading";
                return false;
            }
            count -= static_cast<std::uint64_t>(got);
            if (!sink(buffer, static_cast<std::size_t>(got))) return stopped();
        }
        return true;
    };

    if (head.status == 204 || head.status == 304 || (head.status >= 100 && head.status < 200)) {
        reusable = keepAlive;
        return true;
    }
    if (lowerAscii(head.header("Transfer-Encoding")).find("chunked") != std::string::npos) {
        std::string line;
        for (;;) {
            if (!stream.readLine(line)) {
                error = "the connection broke while reading";
                return false;
            }
            const std::uint64_t size = std::strtoull(line.c_str(), nullptr, 16);  // (a chunk extension after ';' is ignored)
            if (size == 0) break;
            if (!readExactly(size)) return false;
            if (!stream.readLine(line)) {  // the line end after the chunk
                error = "the connection broke while reading";
                return false;
            }
        }
        while (stream.readLine(line) && !line.empty()) {  // (trailers)
        }
        reusable = keepAlive;
        return true;
    }
    if (const std::string length = head.header("Content-Length"); !length.empty()) {
        if (!readExactly(std::strtoull(length.c_str(), nullptr, 10))) return false;
        reusable = keepAlive;
        return true;
    }
    for (;;) {  // until the server closes the connection
        const int got = stream.read(buffer, sizeof buffer);
        if (got == 0) return true;
        if (got < 0) {
            error = "the connection broke while reading";
            return false;
        }
        if (!sink(buffer, static_cast<std::size_t>(got))) return stopped();
    }
}

bool parseHttpsUrl(const std::string& url, HttpsUrl& out) {
    constexpr const char* kScheme = "https://";
    if (url.compare(0, 8, kScheme) != 0) return false;
    const std::size_t hostStart = 8;
    const std::size_t pathStart = url.find_first_of("/?#", hostStart);
    std::string authority = url.substr(hostStart, pathStart == std::string::npos ? std::string::npos : pathStart - hostStart);
    if (authority.empty() || authority.find('@') != std::string::npos) return false;  // (no user names in addresses)
    out.port = 443;
    if (const std::size_t colon = authority.rfind(':'); colon != std::string::npos && authority.find(']') == std::string::npos) {
        out.port = std::atoi(authority.c_str() + colon + 1);
        authority.resize(colon);
        if (out.port <= 0 || out.port > 65535) return false;
    }
    out.host = authority;
    std::string rest = pathStart == std::string::npos ? "/" : url.substr(pathStart);
    if (const std::size_t fragment = rest.find('#'); fragment != std::string::npos) rest.resize(fragment);
    if (rest.empty() || rest[0] != '/') rest.insert(rest.begin(), '/');
    out.path = rest;
    return !out.host.empty();
}

}  // namespace falcon
