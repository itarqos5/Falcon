package com.mojang.brigadier.arguments;

import com.mojang.brigadier.context.CommandContext;

public class IntegerArgumentType implements ArgumentType<Integer> {
    private final int min, max;

    private IntegerArgumentType(int min, int max) { this.min = min; this.max = max; }

    public static IntegerArgumentType integer() { return new IntegerArgumentType(Integer.MIN_VALUE, Integer.MAX_VALUE); }
    public static IntegerArgumentType integer(int min) { return new IntegerArgumentType(min, Integer.MAX_VALUE); }
    public static IntegerArgumentType integer(int min, int max) { return new IntegerArgumentType(min, max); }
    public static int getInteger(CommandContext<?> context, String name) { return context.getArgument(name, Integer.class); }

    @Override
    public Integer parse(String input, int[] used) {
        int space = input.indexOf(' ');
        String word = space < 0 ? input : input.substring(0, space);
        try {
            int v = Integer.parseInt(word);
            if (v < min || v > max) return null;
            used[0] = word.length();
            return v;
        } catch (NumberFormatException e) { return null; }
    }
}
