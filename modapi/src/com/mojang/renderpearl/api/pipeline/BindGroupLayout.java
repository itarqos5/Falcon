package com.mojang.renderpearl.api.pipeline;

import java.util.ArrayList;
import java.util.List;

public class BindGroupLayout {
    private final String name;
    private final List<String> entries = new ArrayList<>();

    public BindGroupLayout(String name) { this.name = name; }
    public static Builder builder() { return new Builder(); }
    public String name() { return name; }

    public static class Builder {
        private final BindGroupLayout layout = new BindGroupLayout("custom");

        public Builder withUniform(String name, UniformType type) { layout.entries.add(name); return this; }
        public Builder withSampler(String name) { layout.entries.add(name); return this; }
        public BindGroupLayout build() { return layout; }
    }
}
