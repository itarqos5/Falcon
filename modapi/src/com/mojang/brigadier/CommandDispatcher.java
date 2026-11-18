package com.mojang.brigadier;

import com.mojang.brigadier.builder.LiteralArgumentBuilder;
import com.mojang.brigadier.context.CommandContext;
import com.mojang.brigadier.exceptions.CommandSyntaxException;
import com.mojang.brigadier.tree.CommandNode;
import com.mojang.brigadier.tree.LiteralCommandNode;
import java.util.ArrayList;
import java.util.HashMap;
import java.util.List;
import java.util.Map;

public class CommandDispatcher<S> {
    private final CommandNode<S> root = new CommandNode<>("", null);

    public LiteralCommandNode<S> register(LiteralArgumentBuilder<S> command) {
        LiteralCommandNode<S> node = (LiteralCommandNode<S>) command.build();
        root.children.add(node);
        return node;
    }

    public CommandNode<S> getRoot() { return root; }
    public List<String> literals() { List<String> l = new ArrayList<>(); for (CommandNode<S> n : root.children) l.add(n.name); return l; }

    // The command a line ends at: every way the line can be read is tried, in the order the nodes were added, and the first that uses all of it wins.
    private Command<S> find(CommandNode<S> node, String rest, S source, Map<String, Object> arguments) {
        if (rest.isEmpty()) {
            if (node.command != null) return node.command;
            return node.redirect != null && node.redirect != node ? find(node.redirect, rest, source, arguments) : null;
        }
        for (CommandNode<S> child : node.children) {
            if (!child.requirement.test(source)) continue;
            Map<String, Object> saved = new HashMap<>(arguments);
            String after = null;
            if (child.type == null) {
                int space = rest.indexOf(' ');
                String word = space < 0 ? rest : rest.substring(0, space);
                if (word.equals(child.name)) after = space < 0 ? "" : rest.substring(space + 1);
            } else {
                int[] used = {0};
                Object v = child.type.parse(rest, used);
                if (v != null) {
                    arguments.put(child.name, v);
                    after = used[0] >= rest.length() ? "" : rest.substring(used[0] + 1);
                }
            }
            if (after != null) {
                Command<S> found = find(child.redirect != null ? child.redirect : child, after.trim(), source, arguments);
                if (found != null) return found;
            }
            arguments.clear();
            arguments.putAll(saved);
        }
        return null;
    }

    // Runs a command line (without the slash); the command's own result.
    public int execute(String line, S source) throws CommandSyntaxException {
        Map<String, Object> arguments = new HashMap<>();
        Command<S> command = find(root, line.trim(), source, arguments);
        if (command == null) throw new CommandSyntaxException("Unknown or incomplete command: " + line);
        return command.run(new CommandContext<>(source, arguments));
    }
}
