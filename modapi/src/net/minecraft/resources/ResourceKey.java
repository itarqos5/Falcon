package net.minecraft.resources;

public final class ResourceKey<T> implements Comparable<ResourceKey<?>> {
    private final Identifier registry, identifier;

    private ResourceKey(Identifier registry, Identifier identifier) {
        this.registry = registry;
        this.identifier = identifier;
    }

    public static <T> ResourceKey<T> create(ResourceKey<?> registry, Identifier id) { return new ResourceKey<>(registry.identifier, id); }
    public static <T> ResourceKey<net.minecraft.core.Registry<T>> createRegistryKey(Identifier id) { return new ResourceKey<>(Identifier.withDefaultNamespace("root"), id); }

    public Identifier identifier() { return identifier; }
    public Identifier location() { return identifier; }
    public Identifier registry() { return registry; }
    public boolean isFor(ResourceKey<? extends net.minecraft.core.Registry<?>> registryKey) { return registry.equals(registryKey.identifier); }
    @Override public String toString() { return "ResourceKey[" + registry + " / " + identifier + "]"; }
    @Override public boolean equals(Object o) { return o instanceof ResourceKey<?> other && other.registry.equals(registry) && other.identifier.equals(identifier); }
    @Override public int hashCode() { return 31 * registry.hashCode() + identifier.hashCode(); }
    @Override public int compareTo(ResourceKey<?> other) { int c = registry.compareTo(other.registry); return c != 0 ? c : identifier.compareTo(other.identifier); }
}
