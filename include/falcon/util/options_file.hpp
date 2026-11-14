#pragma once

#include <filesystem>
#include <map>
#include <optional>
#include <string>

namespace falcon {

// A tiny "key:value" settings file in the style of the game's options.txt: one pair per line, the
// key up to the first ':'. Unknown keys are kept, so a file written by a newer build survives an
// older one. Reading never fails: a missing or damaged file is just an empty set of settings.
class OptionsFile {
public:
    // Reads `file`; lines without a ':' are ignored.
    static OptionsFile load(const std::filesystem::path& file);
    // Parses the text of a file (used by tests).
    static OptionsFile parse(const std::string& text);

    [[nodiscard]] bool contains(const std::string& key) const { return values_.contains(key); }
    [[nodiscard]] std::string getString(const std::string& key, const std::string& fallback = {}) const;
    [[nodiscard]] int getInt(const std::string& key, int fallback) const;
    [[nodiscard]] double getDouble(const std::string& key, double fallback) const;
    [[nodiscard]] bool getBool(const std::string& key, bool fallback) const;  // "true" / "false"

    void set(const std::string& key, const std::string& value) { values_[key] = value; }
    void set(const std::string& key, int value) { values_[key] = std::to_string(value); }
    void set(const std::string& key, double value);
    void set(const std::string& key, bool value) { values_[key] = value ? "true" : "false"; }

    // Writes the file (keys sorted) via a temporary file and a rename, so a crash mid-save cannot
    // leave a half-written options file. False if it could not be written.
    bool save(const std::filesystem::path& file) const;
    [[nodiscard]] std::string toString() const;

private:
    std::map<std::string, std::string> values_;
};

}  // namespace falcon
