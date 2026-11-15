#include "falcon/net/server_commands.hpp"

namespace falcon::net {

namespace {

constexpr int kStringParser = 5;
constexpr int kMessageParser = 20;

// Skips the properties of an argument type (ArgumentTypeInfo.serializeToNetwork). Most types have none; these are the ones that do.
bool skipProperties(Reader& r, int parser, int& stringMode) {
    switch (parser) {
        case 1: {  // float: which of min and max are there, then them
            const int flags = r.u8();
            if (flags & 1) r.f32();
            if (flags & 2) r.f32();
            return r.ok;
        }
        case 2: {  // double
            const int flags = r.u8();
            if (flags & 1) r.f64();
            if (flags & 2) r.f64();
            return r.ok;
        }
        case 3: {  // integer
            const int flags = r.u8();
            if (flags & 1) r.i32();
            if (flags & 2) r.i32();
            return r.ok;
        }
        case 4: {  // long
            const int flags = r.u8();
            if (flags & 1) r.i64();
            if (flags & 2) r.i64();
            return r.ok;
        }
        case kStringParser: stringMode = r.varint(); return r.ok;  // a word, a phrase in quotes, or the rest of the line
        case 6: r.u8(); return r.ok;                                // entity: one only, players only
        case 31: r.u8(); return r.ok;                               // score holder: several
        case 43: r.i32(); return r.ok;                              // time: the least
        case 44: case 45: case 46: case 47: case 48: (void)r.string(); return r.ok;  // a thing of a registry: which registry
        default: return true;                                       // the others have no properties
    }
}

// The end of the word that starts at `at`: a space ends it, except inside quotes or brackets or braces.
std::size_t wordEnd(const std::string& s, std::size_t at) {
    int depth = 0;
    bool quoted = false;
    char quote = 0;
    for (std::size_t i = at; i < s.size(); ++i) {
        const char c = s[i];
        if (quoted) {
            if (c == '\\') ++i;
            else if (c == quote) quoted = false;
            continue;
        }
        if (c == '"' || c == '\'') {
            quoted = true;
            quote = c;
        } else if (c == '[' || c == '{') {
            ++depth;
        } else if (c == ']' || c == '}') {
            if (depth > 0) --depth;
        } else if (c == ' ' && depth == 0) {
            return i;
        }
    }
    return s.size();
}

}  // namespace

bool ServerCommands::read(Reader& r) {
    nodes_.clear();
    const int count = r.varint();
    if (!r.ok || count < 0 || count > 100000) return false;
    nodes_.resize(static_cast<std::size_t>(count));
    for (Node& node : nodes_) {
        const int flags = r.u8();
        node.kind = flags & 3;
        node.executable = (flags & 4) != 0;
        const int children = r.varint();
        if (!r.ok || children < 0 || children > count) {
            nodes_.clear();
            return false;
        }
        for (int i = 0; i < children; ++i) node.children.push_back(r.varint());
        if (flags & 8) node.redirect = r.varint();
        if (node.kind == 1 || node.kind == 2) node.name = r.string();
        if (node.kind == 2) {
            node.parser = r.varint();
            if (!skipProperties(r, node.parser, node.stringMode)) {
                nodes_.clear();
                return false;
            }
            if (flags & 0x10) (void)r.string();  // the kind of suggestions
        }
        if (!r.ok) {
            nodes_.clear();
            return false;
        }
    }
    root_ = r.varint();
    if (!r.ok || root_ < 0 || root_ >= count) {
        nodes_.clear();
        return false;
    }
    return true;
}

bool ServerCommands::walk(int node, const std::string& text, std::size_t at, std::vector<SignableArgument>& out, int depth) const {
    if (depth > 64) return false;
    if (at >= text.size()) return true;  // nothing is left to match
    const Node& here = nodes_[static_cast<std::size_t>(node)];
    const std::vector<int>& children = here.redirect >= 0 && static_cast<std::size_t>(here.redirect) < nodes_.size() ? nodes_[static_cast<std::size_t>(here.redirect)].children : here.children;
    const std::size_t end = wordEnd(text, at);
    const std::string word = text.substr(at, end - at);
    const std::size_t next = end < text.size() ? end + 1 : text.size();
    for (const int c : children) {  // a literal that is this word comes first (as in Brigadier, which only looks at arguments when no literal fits)
        if (c < 0 || static_cast<std::size_t>(c) >= nodes_.size()) continue;
        const Node& child = nodes_[static_cast<std::size_t>(c)];
        if (child.kind == 1 && child.name == word) {
            std::vector<SignableArgument> found = out;
            if (walk(c, text, next, found, depth + 1)) {
                out = std::move(found);
                return true;
            }
        }
    }
    for (const int c : children) {
        if (c < 0 || static_cast<std::size_t>(c) >= nodes_.size()) continue;
        const Node& child = nodes_[static_cast<std::size_t>(c)];
        if (child.kind != 2) continue;
        if (child.parser == kMessageParser) {  // a message takes everything that is left, and that is what is signed
            out.push_back({child.name, text.substr(at)});
            return true;
        }
        if (child.parser == kStringParser && child.stringMode == 2) return true;  // a greedy string: everything that is left, not signed
        std::vector<SignableArgument> found = out;
        if (walk(c, text, next, found, depth + 1)) {
            out = std::move(found);
            return true;
        }
    }
    return false;
}

std::vector<SignableArgument> ServerCommands::signableArguments(const std::string& command) const {
    std::vector<SignableArgument> out;
    if (nodes_.empty()) return out;
    if (!walk(root_, command, 0, out, 0)) out.clear();
    return out;
}

}  // namespace falcon::net
