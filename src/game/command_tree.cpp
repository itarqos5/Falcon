#include "falcon/game/command_tree.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <fstream>

namespace falcon {

namespace {

using json = nlohmann::json;

std::unique_ptr<CommandNode> readNode(const std::string& name, const json& j, bool inheritRestricted) {
    auto node = std::make_unique<CommandNode>();
    node->name = name;
    const std::string type = j.value("type", std::string("literal"));
    node->kind = type == "argument" ? CommandNode::Kind::Argument : type == "root" ? CommandNode::Kind::Root : CommandNode::Kind::Literal;
    node->parser = j.value("parser", std::string());
    node->executable = j.value("executable", false);
    if (const auto props = j.find("properties"); props != j.end() && props->is_object()) {
        for (const auto& [key, value] : props->items()) {
            if (value.is_string()) node->properties[key] = value.get<std::string>();
            else if (value.is_boolean()) node->properties[key] = value.get<bool>() ? "true" : "false";
            else if (value.is_number_integer()) node->properties[key] = std::to_string(value.get<long long>());
            else if (value.is_number()) node->properties[key] = std::to_string(value.get<double>());
        }
    }
    if (const auto redirect = j.find("redirect"); redirect != j.end() && redirect->is_array()) {
        for (const json& step : *redirect) {
            if (step.is_string()) node->redirect.push_back(step.get<std::string>());
        }
    }
    // "permissions": {"type": "minecraft:require", "permission": {"type": "minecraft:command_level", "level": "gamemasters"}}: what only a game master may run.
    bool restricted = inheritRestricted;
    if (const auto permissions = j.find("permissions"); permissions != j.end() && permissions->is_object()) {
        const std::string text = permissions->dump();
        restricted = restricted || text.find("command_level") != std::string::npos;
    }
    node->restricted = restricted;
    if (const auto children = j.find("children"); children != j.end() && children->is_object()) {
        for (const auto& [childName, child] : children->items()) node->children.push_back(readNode(childName, child, restricted));
    }
    return node;
}

std::string lower(std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

// SharedSuggestionProvider.matchesSubStr: the typed text starts the whole name, or starts the name after a separator ("pick" finds "iron_pickaxe").
bool matchesSubStr(const std::string& partialLower, const std::string& candidate) {
    const std::string c = lower(candidate);
    for (std::size_t i = 0; i < c.size(); ++i) {
        if (i != 0 && c[i - 1] != '.' && c[i - 1] != '_' && c[i - 1] != '/' && c[i - 1] != ':') continue;
        if (c.compare(i, partialLower.size(), partialLower) == 0) return true;
    }
    return partialLower.empty();
}

struct ArgResult {
    bool ok = false;
    bool incomplete = false;  // the text ended before the argument did
    std::size_t end = 0;
    std::string error;
};

std::size_t wordEnd(const std::string& t, std::size_t pos) {
    const std::size_t e = t.find(' ', pos);
    return e == std::string::npos ? t.size() : e;
}

// Past the matching close of a [...] or {...} group that starts at `pos` (nested groups and quoted text are skipped); npos if it is not closed.
std::size_t groupEnd(const std::string& t, std::size_t pos) {
    int depth = 0;
    char quote = 0;
    for (std::size_t i = pos; i < t.size(); ++i) {
        const char c = t[i];
        if (quote != 0) {
            if (c == '\\') ++i;
            else if (c == quote) quote = 0;
            continue;
        }
        if (c == '"' || c == '\'') quote = c;
        else if (c == '[' || c == '{') ++depth;
        else if (c == ']' || c == '}') {
            if (--depth == 0) return i + 1;
        }
    }
    return std::string::npos;
}

bool parseNumber(const std::string& s, bool integer, double& out) {
    if (s.empty()) return false;
    char* end = nullptr;
    out = std::strtod(s.c_str(), &end);
    if (end == nullptr || *end != '\0') return false;
    if (integer && s.find_first_of(".eE") != std::string::npos) return false;
    return true;
}

bool validCoordinate(const std::string& w, bool integer) {
    if (w.empty()) return false;
    if (w[0] == '^' || w[0] == '~') {
        if (w.size() == 1) return true;
        double v;
        return parseNumber(w.substr(1), false, v);
    }
    double v;
    return parseNumber(w, integer, v);
}

std::string withNamespace(const std::string& id) { return id.find(':') == std::string::npos ? "minecraft:" + id : id; }

bool known(const CommandContext& ctx, const std::string& registry, const std::string& id) {
    if (!ctx.names) return true;
    const std::vector<std::string>& all = ctx.names(registry);
    if (all.empty()) return true;  // (a registry nothing is known about: anything goes)
    const std::string full = withNamespace(id);
    return std::find(all.begin(), all.end(), full) != all.end();
}

ArgResult parseArgument(const CommandNode& node, const std::string& text, std::size_t pos, const CommandContext& ctx) {
    ArgResult r;
    const std::string& p = node.parser;
    const std::size_t we = wordEnd(text, pos);
    const std::string word = text.substr(pos, we - pos);
    const auto fail = [&](std::string message) {
        r.ok = false;
        r.end = pos;
        r.error = std::move(message);
        return r;
    };
    const auto done = [&](std::size_t end) {
        r.ok = true;
        r.end = end;
        return r;
    };
    const auto missing = [&] {
        r.ok = false;
        r.incomplete = true;
        r.end = text.size();
        return r;
    };
    if (p == "brigadier:integer" || p == "brigadier:double" || p == "brigadier:float") {
        if (word.empty()) return missing();
        const bool integer = p == "brigadier:integer";
        double v;
        if (!parseNumber(word, integer, v)) return fail(integer ? "Expected integer" : p == "brigadier:float" ? "Expected float" : "Expected double");
        if (node.properties.count("min") != 0 && v < std::atof(node.property("min").c_str())) return fail((integer ? "Integer" : "Value") + std::string(" must not be less than ") + node.property("min") + ", found " + word);
        if (node.properties.count("max") != 0 && v > std::atof(node.property("max").c_str())) return fail((integer ? "Integer" : "Value") + std::string(" must not be more than ") + node.property("max") + ", found " + word);
        return done(we);
    }
    if (p == "brigadier:bool") {
        if (word.empty()) return missing();
        if (word != "true" && word != "false") return fail("Invalid bool, expected true or false but found '" + word + "'");
        return done(we);
    }
    if (p == "brigadier:string") {
        const std::string type = node.property("type", "word");
        if (type == "greedy") return pos >= text.size() ? missing() : done(text.size());
        if (type == "phrase" && !word.empty() && word[0] == '"') {
            const std::size_t close = text.find('"', pos + 1);
            return close == std::string::npos ? missing() : done(close + 1);
        }
        return word.empty() ? missing() : done(we);
    }
    if (p == "minecraft:gamemode") {
        if (word.empty()) return missing();
        if (word != "survival" && word != "creative" && word != "adventure" && word != "spectator") return fail("Invalid game mode '" + word + "'");
        return done(we);
    }
    if (p == "minecraft:entity" || p == "minecraft:game_profile") {
        if (word.empty()) return missing();
        if (word[0] == '@') {
            if (word.size() < 2) return missing();
            if (std::string("aeprsn").find(word[1]) == std::string::npos) return fail("Unknown selector type '" + word.substr(0, 2) + "'");
            std::size_t end = pos + 2;
            if (end < text.size() && text[end] == '[') {
                const std::size_t close = groupEnd(text, end);
                if (close == std::string::npos) return missing();
                end = close;
            }
            return done(end);
        }
        return done(we);
    }
    if (p == "minecraft:item_stack" || p == "minecraft:block_state" || p == "minecraft:block_predicate") {
        if (word.empty()) return missing();
        std::size_t idEnd = pos;
        while (idEnd < text.size() && text[idEnd] != ' ' && text[idEnd] != '[' && text[idEnd] != '{') ++idEnd;
        std::string id = text.substr(pos, idEnd - pos);
        const bool item = p == "minecraft:item_stack";
        if (!(p == "minecraft:block_predicate" && !id.empty() && id[0] == '#') && !known(ctx, item ? "minecraft:item" : "minecraft:block", id)) {
            return fail(std::string("Unknown ") + (item ? "item" : "block type") + " '" + withNamespace(id) + "'");
        }
        std::size_t end = idEnd;
        while (end < text.size() && (text[end] == '[' || text[end] == '{')) {
            const std::size_t close = groupEnd(text, end);
            if (close == std::string::npos) return missing();
            end = close;
        }
        return done(end);
    }
    if (p == "minecraft:block_pos" || p == "minecraft:vec3" || p == "minecraft:column_pos" || p == "minecraft:vec2" || p == "minecraft:rotation") {
        const int count = (p == "minecraft:block_pos" || p == "minecraft:vec3") ? 3 : 2;
        const bool integer = p == "minecraft:block_pos" || p == "minecraft:column_pos";
        std::size_t at = pos;
        for (int i = 0; i < count; ++i) {
            const std::size_t e = wordEnd(text, at);
            const std::string w = text.substr(at, e - at);
            if (w.empty()) return missing();
            if (!validCoordinate(w, integer && !(w[0] == '~' || w[0] == '^'))) return fail(count == 3 ? "Expected a coordinate" : "Expected a coordinate");
            if (i + 1 == count) return done(e);
            if (e >= text.size()) return missing();
            at = e + 1;
        }
    }
    if (p == "minecraft:time") {
        if (word.empty()) return missing();
        std::string number = word;
        if (!number.empty() && (number.back() == 'd' || number.back() == 's' || number.back() == 't')) number.pop_back();
        double v;
        if (!parseNumber(number, false, v)) return fail("Invalid time '" + word + "'");
        return done(we);
    }
    if (p == "minecraft:message" || p == "minecraft:component" || p == "minecraft:style" || p == "minecraft:dialog") return pos >= text.size() ? missing() : done(text.size());
    if (p == "minecraft:nbt_compound_tag") {
        if (pos >= text.size()) return missing();
        if (text[pos] != '{') return fail("Expected '{'");
        const std::size_t close = groupEnd(text, pos);
        return close == std::string::npos ? missing() : done(close);
    }
    if (p == "minecraft:nbt_tag") {
        if (pos >= text.size()) return missing();
        if (text[pos] == '{' || text[pos] == '[') {
            const std::size_t close = groupEnd(text, pos);
            return close == std::string::npos ? missing() : done(close);
        }
        return done(we);
    }
    return word.empty() ? missing() : done(we);  // every other kind (a name, an id, a score holder ...) is one word
}

std::vector<const CommandNode*> visibleChildren(const CommandTree& tree, const CommandNode& node, bool cheats) {
    std::vector<const CommandNode*> out;
    for (const auto& child : tree.continuation(node).children) {
        if (child->restricted && !cheats) continue;
        out.push_back(child.get());
    }
    return out;
}

std::vector<std::string> argumentCandidates(const CommandNode& node, const CommandContext& ctx) {
    const std::string& p = node.parser;
    if (p == "brigadier:bool") return {"false", "true"};
    if (p == "minecraft:gamemode") return {"adventure", "creative", "spectator", "survival"};
    if (p == "minecraft:entity" || p == "minecraft:game_profile") {
        std::vector<std::string> out = {"@a", "@p", "@r", "@s"};
        if (p == "minecraft:entity" && node.property("type") != "players") {
            out.push_back("@e");
            out.push_back("@n");
        }
        if (!ctx.playerName.empty()) out.push_back(ctx.playerName);
        return out;
    }
    if (p == "minecraft:item_stack" && ctx.names) return ctx.names("minecraft:item");
    if ((p == "minecraft:block_state" || p == "minecraft:block_predicate") && ctx.names) return ctx.names("minecraft:block");
    if ((p == "minecraft:resource" || p == "minecraft:resource_key" || p == "minecraft:resource_or_tag" || p == "minecraft:resource_or_tag_key") && ctx.names) {
        const std::string registry = node.property("registry");
        if (!registry.empty()) return ctx.names(registry);
    }
    if (p == "minecraft:block_pos" || p == "minecraft:vec3" || p == "minecraft:column_pos" || p == "minecraft:vec2" || p == "minecraft:rotation") return {"~", "^"};
    return {};
}

// Where the text can end in the tree: the node the next word follows and where that word starts. A word may fit several arguments of a node (teleport takes an
// entity or a position), so every way of reading the text is followed and each place it can end is kept.
struct Walk {
    const CommandNode* parent = nullptr;
    std::size_t tokenStart = 0;
    const CommandNode* pending = nullptr;  // an argument of several words that is half typed
};

void walkText(const CommandTree& tree, const CommandContext& ctx, const std::string& text, const CommandNode& node, std::size_t pos, std::vector<Walk>& out, int depth = 0) {
    if (depth > 64) return;
    if (pos >= text.size()) {
        out.push_back({&node, pos, nullptr});
        return;
    }
    const bool lastWord = text.find(' ', pos) == std::string::npos;
    if (lastWord) {  // the word being typed
        out.push_back({&node, pos, nullptr});
        return;
    }
    for (const CommandNode* child : visibleChildren(tree, node, ctx.cheats)) {
        if (!child->isArgument()) {
            const std::size_t n = child->name.size();
            if (text.compare(pos, n, child->name) == 0 && pos + n < text.size() && text[pos + n] == ' ') walkText(tree, ctx, text, *child, pos + n + 1, out, depth + 1);
            continue;
        }
        const ArgResult r = parseArgument(*child, text, pos, ctx);
        if (r.ok && r.end < text.size() && text[r.end] == ' ') {
            walkText(tree, ctx, text, *child, r.end + 1, out, depth + 1);
        } else if (r.incomplete) {
            out.push_back({&node, text.rfind(' ') + 1, child});
        }
    }
}

std::string smartTail(const CommandTree& tree, const CommandNode& n, bool cheats) {
    if (n.redirectsToRoot) return "<command>";
    if (!n.redirect.empty()) return "-> " + n.redirect.back();
    const std::vector<const CommandNode*> kids = visibleChildren(tree, n, cheats);
    if (kids.empty()) return "";
    const bool optional = n.executable;
    const auto usageOf = [&](const CommandNode& c) {
        std::string s = c.usageText();
        const std::string t = smartTail(tree, c, cheats);
        if (!t.empty()) s += " " + t;
        return s;
    };
    if (kids.size() == 1) {
        const std::string s = usageOf(*kids[0]);
        return optional ? "[" + s + "]" : s;
    }
    std::string inner;
    for (std::size_t i = 0; i < kids.size(); ++i) inner += (i == 0 ? "" : "|") + usageOf(*kids[i]);
    inner = "(" + inner + ")";
    return optional ? "[" + inner + "]" : inner;
}

}  // namespace

std::shared_ptr<const CommandTree> CommandTree::loadFromGameData(const std::filesystem::path& gameDataDir, std::string& error) {
    std::ifstream in(gameDataDir / "commands.json", std::ios::binary);
    if (!in) {
        error = "commands.json is missing";
        return nullptr;
    }
    const json doc = json::parse(in, nullptr, /*allow_exceptions=*/false);
    if (!doc.is_object() || !doc.contains("children")) {
        error = "commands.json is not what was expected";
        return nullptr;
    }
    auto tree = std::make_shared<CommandTree>();
    tree->root_ = std::move(*readNode("", doc, false));
    tree->root_.kind = CommandNode::Kind::Root;
    // "run" ends the modifiers of /execute and starts a command of its own; the export writes it as a plain word, so it is marked here.
    const std::function<void(CommandNode&)> mark = [&](CommandNode& n) {
        if (n.name == "run" && n.kind == CommandNode::Kind::Literal && n.children.empty() && !n.executable && n.redirect.empty()) n.redirectsToRoot = true;
        for (auto& child : n.children) mark(*child);
    };
    mark(tree->root_);
    return tree;
}

const CommandNode* CommandTree::resolve(const std::vector<std::string>& path) const {
    const CommandNode* node = &root_;
    for (const std::string& step : path) {
        const CommandNode* next = nullptr;
        for (const auto& child : node->children) {
            if (child->name == step) next = child.get();
        }
        if (next == nullptr) return nullptr;
        node = next;
    }
    return node;
}

const CommandNode& CommandTree::continuation(const CommandNode& node) const {
    if (node.redirectsToRoot) return root_;
    if (node.redirect.empty()) return node;
    const CommandNode* target = resolve(node.redirect);
    return target != nullptr ? *target : node;
}

namespace {

struct ParseSearch {
    const CommandTree& tree;
    const CommandContext& ctx;
    const std::string& text;
    CommandParse best;  // the best parse so far: the complete one, or the one that got furthest
    bool done = false;
    bool haveBest = false;

    void fail(const std::vector<CommandPart>& parts, std::size_t at, const std::string& error) {
        if (haveBest && at <= best.errorAt) return;
        haveBest = true;
        best = CommandParse();
        best.parts = parts;
        best.last = parts.empty() ? &tree.root() : parts.back().node;
        best.end = at;
        best.errorAt = at;
        best.error = error;
    }

    bool descend(const CommandNode& node, std::size_t pos, std::vector<CommandPart>& parts, int depth) {
        if (depth > 64) return false;
        std::string argumentError;
        bool matchedAny = false;
        // Literals first, then the arguments, as the game tries its nodes.
        std::vector<const CommandNode*> candidates = visibleChildren(tree, node, ctx.cheats);
        std::stable_sort(candidates.begin(), candidates.end(), [](const CommandNode* a, const CommandNode* b) { return !a->isArgument() && b->isArgument(); });
        for (const CommandNode* child : candidates) {
            std::size_t end = 0;
            if (!child->isArgument()) {
                const std::size_t n = child->name.size();
                if (!(text.compare(pos, n, child->name) == 0 && (pos + n == text.size() || text[pos + n] == ' '))) continue;
                end = pos + n;
            } else {
                const ArgResult a = parseArgument(*child, text, pos, ctx);
                if (!a.ok) {
                    if (argumentError.empty() && !a.incomplete) argumentError = a.error;
                    continue;
                }
                if (!(a.end == text.size() || text[a.end] == ' ')) {
                    if (argumentError.empty()) argumentError = "Expected whitespace to end one argument, but found trailing data";
                    continue;
                }
                end = a.end;
            }
            matchedAny = true;
            parts.push_back({pos, end, child});
            if (end == text.size()) {
                if (child->executable) {
                    best = CommandParse();
                    best.parts = parts;
                    best.last = child;
                    best.end = end;
                    best.complete = true;
                    haveBest = true;
                    return true;
                }
                fail(parts, end, "command.unknown.command");  // incomplete
            } else if (end + 1 >= text.size()) {
                fail(parts, end, "command.unknown.command");  // a space and nothing after it
            } else if (descend(*child, end + 1, parts, depth + 1)) {
                return true;
            }
            parts.pop_back();
        }
        if (!matchedAny) fail(parts, pos, &node == &tree.root() ? "command.unknown.command" : !argumentError.empty() ? argumentError : "command.unknown.argument");
        return false;
    }
};

}  // namespace

CommandParse parseCommand(const CommandTree& tree, const CommandContext& ctx, const std::string& text) {
    ParseSearch search{tree, ctx, text, {}, false, false};
    std::vector<CommandPart> parts;
    if (text.empty()) {
        CommandParse r;
        r.last = &tree.root();
        r.error = "command.unknown.command";
        return r;
    }
    search.descend(tree.root(), 0, parts, 0);
    if (!search.haveBest) {
        search.best.last = &tree.root();
        search.best.error = "command.unknown.command";
    }
    return search.best;
}

CommandSuggestions suggestCommands(const CommandTree& tree, const CommandContext& ctx, const std::string& text) {
    CommandSuggestions out;
    std::vector<Walk> ends;
    walkText(tree, ctx, text, tree.root(), 0, ends);
    out.start = text.size();
    for (const Walk& w : ends) {
        const std::string partial = text.substr(w.tokenStart);
        const std::string partialLower = lower(partial);
        if (out.parent == nullptr || w.tokenStart < out.start) {
            out.start = w.tokenStart;
            out.parent = w.parent;
        }
        std::vector<const CommandNode*> from;
        if (w.pending != nullptr) from.push_back(w.pending);
        else from = visibleChildren(tree, *w.parent, ctx.cheats);
        for (const CommandNode* child : from) {
            if (!child->isArgument()) {
                if (lower(child->name).compare(0, partialLower.size(), partialLower) == 0) out.list.push_back(child->name);
                continue;
            }
            const bool names = child->parser == "minecraft:item_stack" || child->parser == "minecraft:block_state" || child->parser == "minecraft:block_predicate" ||
                               child->parser == "minecraft:resource" || child->parser == "minecraft:resource_key" || child->parser == "minecraft:resource_or_tag" ||
                               child->parser == "minecraft:resource_or_tag_key";
            for (const std::string& candidate : argumentCandidates(*child, ctx)) {
                if (names ? matchesSubStr(partialLower, candidate) : lower(candidate).compare(0, partialLower.size(), partialLower) == 0) out.list.push_back(candidate);
            }
        }
    }
    // (every ending is the same word being typed, so what a suggestion replaces starts at the same place)
    std::sort(out.list.begin(), out.list.end(), [](const std::string& a, const std::string& b) { return lower(a) < lower(b); });
    out.list.erase(std::unique(out.list.begin(), out.list.end()), out.list.end());
    return out;
}

std::vector<std::string> commandUsage(const CommandTree& tree, const CommandContext& ctx, const std::string& text) {
    std::vector<std::string> out;
    std::vector<Walk> ends;
    walkText(tree, ctx, text, tree.root(), 0, ends);
    for (const Walk& w : ends) {
        const std::string partialLower = lower(text.substr(w.tokenStart));
        std::vector<const CommandNode*> from;
        if (w.pending != nullptr) from.push_back(w.pending);
        else from = visibleChildren(tree, *w.parent, ctx.cheats);
        for (const CommandNode* child : from) {
            if (!child->isArgument() && lower(child->name).compare(0, partialLower.size(), partialLower) != 0) continue;
            std::string s = child->usageText();
            const std::string tail = smartTail(tree, *child, ctx.cheats);
            if (!tail.empty()) s += " " + tail;
            if (std::find(out.begin(), out.end(), s) == out.end()) out.push_back(std::move(s));
        }
    }
    return out;
}

std::string commandUsageLine(const CommandTree& tree, const CommandNode& command, bool cheats) {
    std::string s = "/" + command.name;
    const std::string t = smartTail(tree, command, cheats);
    if (!t.empty()) s += " " + t;
    return s;
}

}  // namespace falcon
