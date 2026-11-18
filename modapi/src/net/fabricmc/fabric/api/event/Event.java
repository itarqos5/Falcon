package net.fabricmc.fabric.api.event;

import net.minecraft.resources.Identifier;

public abstract class Event<T> {
    protected volatile T invoker;

    public final T invoker() { return invoker; }
    public abstract void register(T listener);
    public void register(Identifier phase, T listener) { register(listener); }
    public void addPhaseOrdering(Identifier first, Identifier second) {}
}
