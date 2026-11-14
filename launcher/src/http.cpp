#include "http.hpp"

#include "util.hpp"

#include <windows.h>
#include <winhttp.h>

#include <algorithm>
#include <fstream>
#include <unordered_map>

namespace launcher {

namespace fs = std::filesystem;

std::string urlEncode(const std::string& text) {
    static const char* digits = "0123456789ABCDEF";
    std::string out;
    for (const unsigned char c : text) {
        if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.' || c == '~') {
            out.push_back(static_cast<char>(c));
        } else {
            out.push_back('%');
            out.push_back(digits[c >> 4]);
            out.push_back(digits[c & 15]);
        }
    }
    return out;
}

std::string urlDecode(const std::string& text) {
    const auto hex = [](char c) -> int {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
        return -1;
    };
    std::string out;
    for (std::size_t i = 0; i < text.size(); ++i) {
        if (text[i] == '+') {
            out.push_back(' ');
        } else if (text[i] == '%' && i + 2 < text.size() && hex(text[i + 1]) >= 0 && hex(text[i + 2]) >= 0) {
            out.push_back(static_cast<char>(hex(text[i + 1]) * 16 + hex(text[i + 2])));
            i += 2;
        } else {
            out.push_back(text[i]);
        }
    }
    return out;
}

std::string formEncode(const std::vector<std::pair<std::string, std::string>>& fields) {
    std::string out;
    for (const auto& [k, v] : fields) {
        if (!out.empty()) out.push_back('&');
        out += urlEncode(k) + "=" + urlEncode(v);
    }
    return out;
}

namespace {

class WinHttpClient final : public HttpClient {
public:
    explicit WinHttpClient(const std::string& userAgent) {
        session_ = WinHttpOpen(widen(userAgent).c_str(), WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
        if (session_ != nullptr) {
            WinHttpSetTimeouts(session_, 15000, 15000, 30000, 60000);
            DWORD protocols = WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_2 | 0x00002000;  // TLS 1.2 and 1.3 (the 1.3 flag is newer than some headers)
            WinHttpSetOption(session_, WINHTTP_OPTION_SECURE_PROTOCOLS, &protocols, sizeof(protocols));
            DWORD decompress = WINHTTP_DECOMPRESSION_FLAG_ALL;
            WinHttpSetOption(session_, WINHTTP_OPTION_DECOMPRESSION, &decompress, sizeof(decompress));
        }
    }
    ~WinHttpClient() override {
        if (session_ != nullptr) WinHttpCloseHandle(session_);
    }

    HttpResponse request(const std::string& method, const std::string& url, const HttpHeaders& headers, const std::string& body) override {
        HttpResponse out;
        Request r;
        if (!open(r, method, url, headers, body, out.error)) return out;
        out.status = r.status;
        for (;;) {
            DWORD available = 0;
            if (!WinHttpQueryDataAvailable(r.request, &available)) {
                out.error = "the connection broke while reading";
                out.status = 0;
                return out;
            }
            if (available == 0) break;
            const std::size_t at = out.body.size();
            out.body.resize(at + available);
            DWORD read = 0;
            if (!WinHttpReadData(r.request, out.body.data() + at, available, &read)) {
                out.error = "the connection broke while reading";
                out.status = 0;
                return out;
            }
            out.body.resize(at + read);
        }
        return out;
    }

    HttpResponse download(const std::string& url, const fs::path& to, const std::function<void(std::uint64_t, std::uint64_t)>& progress,
                          const std::atomic<bool>* cancel) override {
        HttpResponse out;
        Request r;
        if (!open(r, "GET", url, {}, {}, out.error)) return out;
        out.status = r.status;
        if (!out.ok()) return out;
        std::error_code ec;
        fs::create_directories(to.parent_path(), ec);
        fs::path part = to;
        part += ".part";
        std::ofstream file(part, std::ios::binary | std::ios::trunc);
        if (!file) {
            out.status = 0;
            out.error = "cannot write " + narrow(part.wstring());
            return out;
        }
        wchar_t lengthText[32] = {};
        DWORD lengthSize = sizeof(lengthText);
        std::uint64_t total = 0;
        if (WinHttpQueryHeaders(r.request, WINHTTP_QUERY_CONTENT_LENGTH, WINHTTP_HEADER_NAME_BY_INDEX, lengthText, &lengthSize, WINHTTP_NO_HEADER_INDEX)) {
            total = _wcstoui64(lengthText, nullptr, 10);
        }
        std::vector<char> buffer(64 * 1024);
        std::uint64_t done = 0;
        for (;;) {
            if (cancel != nullptr && cancel->load()) {
                out.status = 0;
                out.error = "cancelled";
                break;
            }
            DWORD read = 0;
            if (!WinHttpReadData(r.request, buffer.data(), static_cast<DWORD>(buffer.size()), &read)) {
                out.status = 0;
                out.error = "the connection broke during the download";
                break;
            }
            if (read == 0) break;
            file.write(buffer.data(), read);
            done += read;
            if (progress) progress(done, total);
        }
        file.close();
        if (out.status == 0 || !file) {
            fs::remove(part, ec);
            if (out.status != 0) {
                out.status = 0;
                out.error = "cannot write the file";
            }
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
    struct Request {
        HINTERNET request = nullptr;
        int status = 0;
        ~Request() {
            if (request != nullptr) WinHttpCloseHandle(request);
        }
    };

    // One connection per host per thread, kept for the next request (an install makes thousands to the same two hosts).
    HINTERNET connectionFor(const std::wstring& host, INTERNET_PORT port) {
        struct Cache {
            std::unordered_map<std::wstring, HINTERNET> map;
            ~Cache() {
                for (auto& [k, h] : map) WinHttpCloseHandle(h);
            }
        };
        thread_local Cache cache;
        const std::wstring key = host + L":" + std::to_wstring(port);
        const auto it = cache.map.find(key);
        if (it != cache.map.end()) return it->second;
        HINTERNET c = WinHttpConnect(session_, host.c_str(), port, 0);
        if (c != nullptr) cache.map[key] = c;
        return c;
    }

    bool open(Request& r, const std::string& method, const std::string& url, const HttpHeaders& headers, const std::string& body, std::string& error) {
        if (session_ == nullptr) {
            error = "no network stack";
            return false;
        }
        const std::wstring wide = widen(url);
        URL_COMPONENTS parts{};
        parts.dwStructSize = sizeof(parts);
        wchar_t host[256], path[2048];
        parts.lpszHostName = host;
        parts.dwHostNameLength = static_cast<DWORD>(std::size(host));
        parts.lpszUrlPath = path;
        parts.dwUrlPathLength = static_cast<DWORD>(std::size(path));
        if (!WinHttpCrackUrl(wide.c_str(), static_cast<DWORD>(wide.size()), 0, &parts)) {
            error = "bad address: " + url;
            return false;
        }
        const bool secure = parts.nScheme == INTERNET_SCHEME_HTTPS;
        for (int attempt = 0; attempt < 2; ++attempt) {  // a kept connection may have been closed by the server: once more on a fresh one
            HINTERNET connection = connectionFor(parts.lpszHostName, parts.nPort);
            if (connection == nullptr) {
                error = "could not connect to " + narrow(parts.lpszHostName);
                return false;
            }
            r.request = WinHttpOpenRequest(connection, widen(method).c_str(), path, nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, secure ? WINHTTP_FLAG_SECURE : 0);
            if (r.request == nullptr) {
                error = "could not start the request";
                return false;
            }
            std::wstring headerText;
            for (const auto& [k, v] : headers) headerText += widen(k) + L": " + widen(v) + L"\r\n";
            const BOOL sent = WinHttpSendRequest(r.request, headerText.empty() ? WINHTTP_NO_ADDITIONAL_HEADERS : headerText.c_str(), static_cast<DWORD>(headerText.size()),
                                                 body.empty() ? WINHTTP_NO_REQUEST_DATA : const_cast<char*>(body.data()), static_cast<DWORD>(body.size()), static_cast<DWORD>(body.size()), 0);
            if (sent && WinHttpReceiveResponse(r.request, nullptr)) {
                DWORD status = 0, size = sizeof(status);
                WinHttpQueryHeaders(r.request, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX, &status, &size, WINHTTP_NO_HEADER_INDEX);
                r.status = static_cast<int>(status);
                return true;
            }
            const DWORD code = GetLastError();
            WinHttpCloseHandle(r.request);
            r.request = nullptr;
            if (attempt == 0 && (code == ERROR_WINHTTP_CONNECTION_ERROR || code == ERROR_WINHTTP_OPERATION_CANCELLED || code == ERROR_WINHTTP_SECURE_FAILURE)) {
                continue;
            }
            switch (code) {
                case ERROR_WINHTTP_TIMEOUT: error = "the server took too long to answer"; break;
                case ERROR_WINHTTP_NAME_NOT_RESOLVED: error = "no internet connection (could not find " + narrow(parts.lpszHostName) + ")"; break;
                case ERROR_WINHTTP_CANNOT_CONNECT: error = "could not connect to " + narrow(parts.lpszHostName); break;
                case ERROR_WINHTTP_SECURE_FAILURE: error = "the secure connection to " + narrow(parts.lpszHostName) + " failed (check the PC's date and time)"; break;
                default: error = "network error " + std::to_string(code); break;
            }
            return false;
        }
        return false;
    }

    HINTERNET session_ = nullptr;
};

}  // namespace

std::unique_ptr<HttpClient> makeWinHttpClient(const std::string& userAgent) { return std::make_unique<WinHttpClient>(userAgent); }

}  // namespace launcher
