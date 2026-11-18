package com.mojang.brigadier.tree;

public class LiteralCommandNode<S> extends CommandNode<S> {
    public LiteralCommandNode(String name) { super(name, null); }
}
