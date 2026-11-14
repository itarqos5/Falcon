#pragma once

// A small JSON tree for the game's worldgen data. It is built by nlohmann/json's SAX parser, but unlike a
// nlohmann DOM it keeps every number as its ORIGINAL TEXT: the game parses a float field with
// Float.parseFloat(text) (correctly rounded), which can differ from (float)Double.parseDouble(text) in rare
// double-rounding cases, and "bit exact" means exactly that. Numbers are converted on demand with
// std::from_chars (correctly rounded, independent of the C runtime).

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace falcon::worldgen {

struct JsonValue {
    enum class Kind : std::uint8_t { Null, Bool, Number, String, Array, Object };

    Kind kind = Kind::Null;
    bool boolean = false;
    std::string text;  // string contents, or the number's source text
    std::vector<JsonValue> items;                              // Array
    std::vector<std::pair<std::string, JsonValue>> members;    // Object, in file order

    [[nodiscard]] bool isNumber() const { return kind == Kind::Number; }
    [[nodiscard]] bool isString() const { return kind == Kind::String; }
    [[nodiscard]] bool isArray() const { return kind == Kind::Array; }
    [[nodiscard]] bool isObject() const { return kind == Kind::Object; }
    [[nodiscard]] const JsonValue* find(std::string_view key) const;

    // Number conversions; false when the value is not a number (or the text does not convert).
    [[nodiscard]] bool toDouble(double& out) const;
    [[nodiscard]] bool toFloat(float& out) const;
    [[nodiscard]] bool toInt(std::int32_t& out) const;
};

[[nodiscard]] bool parseJson(std::string_view text, JsonValue& out, std::string& error);
[[nodiscard]] bool parseJsonFile(const std::filesystem::path& path, JsonValue& out, std::string& error);

}  // namespace falcon::worldgen
