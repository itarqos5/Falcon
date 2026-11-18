package com.mojang.brigadier.tree;

import com.mojang.brigadier.Command;
import com.mojang.brigadier.arguments.ArgumentType;
import java.util.ArrayList;
import java.util.List;
import java.util.function.Predicate;

public class CommandNode<S> {
    public final String name;
    public final ArgumentType<?> type;  // null for a literal
    public Command<S> command;
    public Predicate<S> requirement = s -> true;
    public CommandNode<S> redirect;
    public final List<CommandNode<S>> children = new ArrayList<>();

    public CommandNode(String name, ArgumentType<?> type) { this.name = name; this.type = type; }
    public String getName() { return name; }
}
