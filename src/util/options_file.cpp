#include "falcon/util/options_file.hpp"

#include <charconv>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <sstream>
#include <system_error>

namespace falcon {
namespace fs = std::filesystem;

OptionsFile OptionsFile::parse(const std::string& text) {
    OptionsFile file;
    std::istringstream lines(text);
    std::string line;
    while (std::getline(lines, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        const auto colon = line.find(':');
        if (colon == std::string::npos || colon == 0) continue;
        file.values_[line.substr(0, colon)] = line.substr(colon + 1);
    }
    return file;
}

OptionsFile OptionsFile::load(const fs::path& file) {
    std::ifstream in(file, std::ios::binary);
    if (!in) return {};
    return parse(std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()));
}

std::string OptionsFile::getString(const std::string& key, const std::string& fallback) const {
    const auto it = values_.find(key);
    return it != values_.end() ? it->second : fallback;
}

int OptionsFile::getInt(const std::string& key, int fallback) const {
    const auto it = values_.find(key);
    if (it == values_.end()) return fallback;
    int value = 0;
    const char* end = it->second.data() + it->second.size();
    const auto [ptr, ec] = std::from_chars(it->second.data(), end, value);
    return ec == std::errc() && ptr == end ? value : fallback;
}

double OptionsFile::getDouble(const std::string& key, double fallback) const {
    const auto it = values_.find(key);
    if (it == values_.end()) return fallback;
    try {
        std::size_t used = 0;
        const double value = std::stod(it->second, &used);
        return used == it->second.size() ? value : fallback;
    } catch (const std::exception&) {
        return fallback;
    }
}

bool OptionsFile::getBool(const std::string& key, bool fallback) const {
    const auto it = values_.find(key);
    if (it == values_.end()) return fallback;
    if (it->second == "true") return true;
    if (it->second == "false") return false;
    return fallback;
}

void OptionsFile::set(const std::string& key, double value) {
    char buffer[64];
    std::snprintf(buffer, sizeof(buffer), "%.4f", value);  // plenty for a slider; stable text
    values_[key] = buffer;
}

std::string OptionsFile::toString() const {
    std::string out;
    for (const auto& [key, value] : values_) out += key + ":" + value + "\n";
    return out;
}

bool OptionsFile::save(const fs::path& file) const {
    std::error_code ec;
    if (file.has_parent_path()) fs::create_directories(file.parent_path(), ec);
    const fs::path temporary = fs::path(file) += ".tmp";
    {
        std::ofstream out(temporary, std::ios::binary | std::ios::trunc);
        if (!out) return false;
        const std::string text = toString();
        out.write(text.data(), static_cast<std::streamsize>(text.size()));
        if (!out) return false;
    }
    fs::rename(temporary, file, ec);  // replaces an existing file on Windows too (std::filesystem)
    if (ec) {
        fs::remove(temporary, ec);
        return false;
    }
    return true;
}

}  // namespace falcon
