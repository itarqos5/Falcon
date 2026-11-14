#include "falcon/worldgen/json_tree.hpp"

#include <nlohmann/json.hpp>

#include <charconv>
#include <fstream>
#include <iterator>

namespace falcon::worldgen {

const JsonValue* JsonValue::find(std::string_view key) const {
    if (kind != Kind::Object) return nullptr;
    for (const auto& m : members) {
        if (m.first == key) return &m.second;
    }
    return nullptr;
}

bool JsonValue::toDouble(double& out) const {
    if (kind != Kind::Number) return false;
    const char* first = text.data();
    const char* last = first + text.size();
    if (first != last && *first == '+') ++first;
    const auto r = std::from_chars(first, last, out);
    return r.ec == std::errc() && r.ptr == last;
}

bool JsonValue::toFloat(float& out) const {
    if (kind != Kind::Number) return false;
    const char* first = text.data();
    const char* last = first + text.size();
    if (first != last && *first == '+') ++first;
    const auto r = std::from_chars(first, last, out);
    return r.ec == std::errc() && r.ptr == last;
}

bool JsonValue::toInt(std::int32_t& out) const {
    if (kind != Kind::Number) return false;
    const char* first = text.data();
    const char* last = first + text.size();
    const auto r = std::from_chars(first, last, out);
    if (r.ec == std::errc() && r.ptr == last) return true;
    // "4.0" style integers: accept when the value is integral and in range.
    double d = 0.0;
    if (!toDouble(d) || d != static_cast<double>(static_cast<std::int32_t>(d)) || d < -2147483648.0 || d > 2147483647.0) return false;
    out = static_cast<std::int32_t>(d);
    return true;
}

namespace {

class TreeBuilder {
public:
    using json = nlohmann::json;
    using number_integer_t = json::number_integer_t;
    using number_unsigned_t = json::number_unsigned_t;
    using number_float_t = json::number_float_t;
    using string_t = json::string_t;
    using binary_t = json::binary_t;

    explicit TreeBuilder(JsonValue& root) { stack_.push_back(&root); }

    bool null() { return put(JsonValue::Kind::Null, {}); }
    bool boolean(bool v) {
        JsonValue& n = slot();
        n.kind = JsonValue::Kind::Bool;
        n.boolean = v;
        return true;
    }
    bool number_integer(number_integer_t v) { return put(JsonValue::Kind::Number, std::to_string(v)); }
    bool number_unsigned(number_unsigned_t v) { return put(JsonValue::Kind::Number, std::to_string(v)); }
    bool number_float(number_float_t, const string_t& s) { return put(JsonValue::Kind::Number, s); }
    bool string(string_t& v) { return put(JsonValue::Kind::String, v); }
    bool binary(binary_t&) { return true; }
    bool start_object(std::size_t) {
        JsonValue& n = slot();
        n.kind = JsonValue::Kind::Object;
        stack_.push_back(&n);
        return true;
    }
    bool end_object() {
        stack_.pop_back();
        return true;
    }
    bool start_array(std::size_t) {
        JsonValue& n = slot();
        n.kind = JsonValue::Kind::Array;
        stack_.push_back(&n);
        return true;
    }
    bool end_array() {
        stack_.pop_back();
        return true;
    }
    bool key(string_t& v) {
        pendingKey_ = v;
        return true;
    }
    bool parse_error(std::size_t, const std::string&, const nlohmann::detail::exception& e) {
        error = e.what();
        return false;
    }

    std::string error;

private:
    // The value being filled: the root, or a new element appended to the container on top of the stack.
    JsonValue& slot() {
        JsonValue* top = stack_.back();
        if (stack_.size() == 1 && top->kind == JsonValue::Kind::Null && !rootUsed_) {
            rootUsed_ = true;
            return *top;
        }
        if (top->kind == JsonValue::Kind::Object) {
            top->members.emplace_back(std::move(pendingKey_), JsonValue{});
            return top->members.back().second;
        }
        top->items.emplace_back();
        return top->items.back();
    }
    bool put(JsonValue::Kind kind, std::string text) {
        JsonValue& n = slot();
        n.kind = kind;
        n.text = std::move(text);
        return true;
    }

    std::vector<JsonValue*> stack_;
    std::string pendingKey_;
    bool rootUsed_ = false;
};

}  // namespace

bool parseJson(std::string_view text, JsonValue& out, std::string& error) {
    out = JsonValue{};
    TreeBuilder builder(out);
    const bool ok = nlohmann::json::sax_parse(text, &builder);
    if (!ok) {
        error = builder.error.empty() ? "invalid JSON" : builder.error;
        return false;
    }
    return true;
}

bool parseJsonFile(const std::filesystem::path& path, JsonValue& out, std::string& error) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        error = "cannot open " + path.string();
        return false;
    }
    const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    if (!parseJson(text, out, error)) {
        error = path.string() + ": " + error;
        return false;
    }
    return true;
}

}  // namespace falcon::worldgen
