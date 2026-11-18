package com.mojang.brigadier.arguments;

import com.mojang.brigadier.context.CommandContext;

public class StringArgumentType implements ArgumentType<String> {
    private final int kind;  // 0 word, 1 phrase, 2 greedy

    private StringArgumentType(int kind) { this.kind = kind; }

    public static StringArgumentType word() { return new StringArgumentType(0); }
    public static StringArgumentType string() { return new StringArgumentType(1); }
    public static StringArgumentType greedyString() { return new StringArgumentType(2); }
    public static String getString(CommandContext<?> context, String name) { return context.getArgument(name, String.class); }

    @Override
    public String parse(String input, int[] used) {
        if (input.isEmpty()) return null;
        if (kind == 2) { used[0] = input.length(); return input; }
        if (kind == 1 && input.charAt(0) == '"') {
            int end = input.indexOf('"', 1);
            if (end < 0) return null;
            used[0] = end + 1;
            return input.substring(1, end);
        }
        int space = input.indexOf(' ');
        used[0] = space < 0 ? input.length() : space;
        return input.substring(0, used[0]);
    }
}
