#pragma once

#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace falcon {

// One node of the game's command tree (Brigadier): a word that has to be typed as it is (a literal), or an argument of some kind. The tree is the one the game
// exports (commands.json), so every command and every argument it has is here, whether or not Falcon can run it.
struct CommandNode {
    enum class Kind { Root, Literal, Argument };
    Kind kind = Kind::Literal;
    std::string name;
    std::string parser;                                // "brigadier:integer", "minecraft:entity", ... (arguments only)
    std::map<std::string, std::string> properties;     // the parser's settings ("min", "type": "greedy", "registry", ...)
    bool executable = false;                           // a command may end here
    bool restricted = false;                           // needs the rights of a game master: only offered with cheats on
    std::vector<std::string> redirect;                 // the path of the node whose children follow this one ("execute")
    bool redirectsToRoot = false;                      // "execute ... run": a whole command follows (the export leaves that redirect out)
    std::vector<std::unique_ptr<CommandNode>> children;

    [[nodiscard]] bool isArgument() const { return kind == Kind::Argument; }
    [[nodiscard]] std::string usageText() const { return kind == Kind::Argument ? "<" + name + ">" : name; }
    [[nodiscard]] std::string property(const std::string& key, const std::string& fallback = "") const {
        const auto it = properties.find(key);
        return it == properties.end() ? fallback : it->second;
    }
};

class CommandTree {
public:
    // Reads <gameDataDir>/commands.json. Null and `error` when it is missing or unusable.
    [[nodiscard]] static std::shared_ptr<const CommandTree> loadFromGameData(const std::filesystem::path& gameDataDir, std::string& error);

    [[nodiscard]] const CommandNode& root() const { return root_; }
    [[nodiscard]] const CommandNode* resolve(const std::vector<std::string>& path) const;
    // The node whose children come after `node` (the node itself, or what it redirects to).
    [[nodiscard]] const CommandNode& continuation(const CommandNode& node) const;

private:
    CommandNode root_;
};

// What the argument parsers and the suggestions need to know about the game.
struct CommandContext {
    std::string playerName;
    bool cheats = false;  // commands that need a game master are only there with cheats on
    // The names in a registry ("minecraft:item", "minecraft:block", "minecraft:entity_type"): "minecraft:stone" ... in any order.
    std::function<const std::vector<std::string>&(const std::string& registry)> names;
};

struct CommandPart {
    std::size_t start = 0, end = 0;  // the text of this node
    const CommandNode* node = nullptr;
};

struct CommandParse {
    std::vector<CommandPart> parts;       // the nodes matched, in order
    const CommandNode* last = nullptr;    // the last node matched (the root when none)
    std::size_t end = 0;                  // how much of the text was understood
    bool complete = false;                // all of the text parsed and the last node can end a command
    std::string error;                    // language key or text; empty if none
    std::size_t errorAt = 0;
};

// Parses a command (the text after the "/").
[[nodiscard]] CommandParse parseCommand(const CommandTree& tree, const CommandContext& context, const std::string& text);

struct CommandSuggestions {
    std::size_t start = 0;               // where in the text the word being typed begins: what a suggestion replaces
    std::vector<std::string> list;       // sorted
    const CommandNode* parent = nullptr; // the node the next word follows
};
// What could be typed next, given the text up to the cursor (CommandDispatcher.getCompletionSuggestions).
[[nodiscard]] CommandSuggestions suggestCommands(const CommandTree& tree, const CommandContext& context, const std::string& textBeforeCursor);
// The ways to go on from the node the text ended at, as the game's "smart usage" writes them: "<targets> <item> [<count>]".
[[nodiscard]] std::vector<std::string> commandUsage(const CommandTree& tree, const CommandContext& context, const std::string& textBeforeCursor);
// The usage of the whole command ("/give <targets> <item> [<count>]") for /help.
[[nodiscard]] std::string commandUsageLine(const CommandTree& tree, const CommandNode& command, bool cheats);

}  // namespace falcon
