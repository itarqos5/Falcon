package com.mojang.brigadier.builder;

import com.mojang.brigadier.Command;
import com.mojang.brigadier.tree.CommandNode;
import java.util.ArrayList;
import java.util.List;
import java.util.function.Predicate;

public abstract class ArgumentBuilder<S, T extends ArgumentBuilder<S, T>> {
    protected final List<ArgumentBuilder<S, ?>> arguments = new ArrayList<>();
    protected Command<S> command;
    protected Predicate<S> requirement = s -> true;
    protected CommandNode<S> redirect;

    protected abstract T getThis();
    public abstract CommandNode<S> build();

    public T then(ArgumentBuilder<S, ?> argument) { arguments.add(argument); return getThis(); }
    public T executes(Command<S> command) { this.command = command; return getThis(); }
    public T requires(Predicate<S> requirement) { this.requirement = requirement; return getThis(); }
    public T redirect(CommandNode<S> target) { this.redirect = target; return getThis(); }

    protected void fill(CommandNode<S> node) {
        node.command = command;
        node.requirement = requirement;
        node.redirect = redirect;
        for (ArgumentBuilder<S, ?> a : arguments) node.children.add(a.build());
    }
}
