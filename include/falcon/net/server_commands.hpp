#pragma once

#include "falcon/net/bytes.hpp"

#include <string>
#include <vector>

namespace falcon::net {

// An argument of a typed command that has to be signed: its name in the command tree ("message") and the text of it.
struct SignableArgument {
    std::string name;
    std::string value;
};

// The command tree a server sends (ClientboundCommandsPacket): nodes that are the root, a literal word or an argument of some type, with their children and where a node redirects
// to. It is kept to answer one question: which arguments of a command that was typed are messages (the argument type "minecraft:message"), because a server that enforces secure
// profiles wants exactly those signed (ServerboundChatCommandSignedPacket) and the command as a whole is not.
class ServerCommands {
public:
    // The packet's body. False (and the tree is empty) when it does not read, e.g. an argument type whose properties are not known.
    bool read(Reader& r);
    [[nodiscard]] bool empty() const { return nodes_.empty(); }
    // The signable arguments of `command` (without the slash), found by walking the tree with the words of the command: literals are matched by name, other arguments take one word
    // (a quoted text, or one in brackets or braces, counts as one), a message takes the rest. Empty when the command does not need signing or does not fit the tree.
    [[nodiscard]] std::vector<SignableArgument> signableArguments(const std::string& command) const;

private:
    struct Node {
        int kind = 0;  // 0 root, 1 literal, 2 argument
        bool executable = false;
        std::vector<int> children;
        int redirect = -1;
        std::string name;
        int parser = -1;
        int stringMode = 0;  // a brigadier string: 0 a word, 1 a phrase, 2 everything that is left
    };
    bool walk(int node, const std::string& text, std::size_t at, std::vector<SignableArgument>& out, int depth) const;
    std::vector<Node> nodes_;
    int root_ = 0;
};

}  // namespace falcon::net
