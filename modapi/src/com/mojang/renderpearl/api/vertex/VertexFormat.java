package com.mojang.renderpearl.api.vertex;

public final class VertexFormat {
    private final String name;

    public VertexFormat(String name) { this.name = name; }
    public String name() { return name; }
    @Override public String toString() { return name; }
}
