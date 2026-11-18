package com.mojang.brigadier.arguments;

import com.mojang.brigadier.context.CommandContext;

public class DoubleArgumentType implements ArgumentType<Double> {
    private final double min, max;

    private DoubleArgumentType(double min, double max) { this.min = min; this.max = max; }

    public static DoubleArgumentType doubleArg() { return new DoubleArgumentType(-Double.MAX_VALUE, Double.MAX_VALUE); }
    public static DoubleArgumentType doubleArg(double min) { return new DoubleArgumentType(min, Double.MAX_VALUE); }
    public static DoubleArgumentType doubleArg(double min, double max) { return new DoubleArgumentType(min, max); }
    public static double getDouble(CommandContext<?> context, String name) { return context.getArgument(name, Double.class); }

    @Override
    public Double parse(String input, int[] used) {
        int space = input.indexOf(' ');
        String word = space < 0 ? input : input.substring(0, space);
        try {
            double v = Double.parseDouble(word);
            if (v < min || v > max) return null;
            used[0] = word.length();
            return v;
        } catch (NumberFormatException e) { return null; }
    }
}
