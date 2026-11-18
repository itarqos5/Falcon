package com.example.test;

import java.util.*;
import java.util.function.*;
import net.fabricmc.api.ClientModInitializer;
import net.fabricmc.api.ModInitializer;
import net.fabricmc.fabric.api.client.event.lifecycle.v1.ClientTickEvents;
import net.minecraft.core.Registry;
import net.minecraft.core.registries.BuiltInRegistries;
import net.minecraft.network.chat.Component;
import net.minecraft.resources.Identifier;
import net.minecraft.world.item.Item;

// Every line of onInitialize does a little work and registers an object whose id says what came out, so the test can read the answers off the registrations.
public class TestMod implements ModInitializer, ClientModInitializer {
    static int started = Counter.next();

    interface Greeter { String name(); default String greet() { return "hi " + name(); } }
    enum Mode { FAST, SLOW; String label() { return name().toLowerCase(); } }
    record Point(int x, int y) {}
    static abstract class Shape {
        abstract double area();
        String describe() { return getClass().getSimpleName() + ":" + area(); }
    }
    static class Square extends Shape { final double s; Square(double s) { this.s = s; } double area() { return s * s; } }
    static class Rect extends Shape { final double w, h; Rect(double w, double h) { this.w = w; this.h = h; } double area() { return w * h; } }

    static void out(String text) { Registry.register(BuiltInRegistries.BLOCK, Identifier.fromNamespaceAndPath("test", text), new Object()); }

    static int fib(int n) { int a = 0, b = 1; for (int i = 0; i < n; i++) { int t = a + b; a = b; b = t; } return a; }
    static long fact(int n) { long r = 1; for (int i = 2; i <= n; i++) r *= i; return r; }
    static int sumTo(int n) { int s = 0; for (int i = 1; i <= n; i++) s += i; return s; }
    static int mix(int a, int b) { return (a * b) + (a / b) + (a % b) + (a << b) + (a >> 1) + (a ^ b) + (a & b) + (a | b); }

    @Override
    public void onInitialize() {
        out("fib_" + fib(10));
        out("sum_" + sumTo(100));
        out("fact_" + fact(10));
        out("mix_" + mix(7, 3));
        out("sqrt_" + Math.sqrt(144.0));
        out("big_" + ((1L << 40) + 1));
        out("conv_" + (byte) 200 + "_" + (char) ('a' + 1) + "_" + (int) 3.99 + "_" + (long) -2.5);
        out("str_" + "Hello".toUpperCase() + "_" + "a,b,c".split(",").length + "_" + new StringBuilder("abc").reverse());

        List<String> list = new ArrayList<>();
        list.add("b"); list.add("a"); list.add("c");
        StringBuilder joined = new StringBuilder();
        list.forEach(s -> joined.append(s));
        out("list_" + joined + "_" + list.size() + "_" + list.contains("a") + "_" + list.indexOf("c"));

        Map<String, Integer> map = new HashMap<>();
        map.put("one", 1); map.put("two", 2); map.put("one", 11);
        out("map_" + map.size() + "_" + map.get("one") + "_" + map.getOrDefault("three", -1));

        out("enum_" + Mode.values().length + "_" + Mode.valueOf("SLOW").ordinal() + "_" + Mode.FAST.label());
        Point p = new Point(3, 4);
        out("rec_" + p + "_" + p.equals(new Point(3, 4)) + "_" + p.equals(new Point(4, 3)));

        String key = "beta";
        int picked = switch (key) { case "alpha" -> 1; case "beta" -> 2; default -> 0; };
        out("switch_" + picked);

        String caught = "none";
        try { int[] a = new int[2]; a[5] = 1; } catch (ArrayIndexOutOfBoundsException e) { caught = e.getMessage(); } finally { caught += "!"; }
        out("array_" + caught);
        try { Integer.parseInt("x"); } catch (RuntimeException e) { out("exc_" + e.getClass().getSimpleName()); }
        try { Object o = "s"; Integer i = (Integer) o; out("no_" + i); } catch (ClassCastException e) { out("cast_ok"); }
        try { throw new IllegalStateException("boom"); } catch (IllegalStateException | IllegalArgumentException e) { out("throw_" + e.getMessage()); }

        out("shape_" + new Square(4).describe() + "_" + new Rect(2, 5).describe());
        Greeter g = () -> "bob";
        out("greet_" + g.greet());
        int base = 10;
        Function<Integer, Integer> add = x -> x + base;
        Supplier<String> sup = () -> "sup" + base;
        BiFunction<Integer, Integer, Integer> mul = (x, y) -> x * y;
        out("lambda_" + add.apply(5) + "_" + sup.get() + "_" + mul.apply(6, 7));
        int[][] grid = new int[3][4];
        grid[2][3] = 7;
        out("grid_" + grid.length + "x" + grid[0].length + "_" + grid[2][3]);
        out("counter_" + Counter.next());
        out("fmt_" + String.format("%s-%d-%.2f", "a", 42, 3.14159));

        System.out.println("booted");
        Registry.register(BuiltInRegistries.ITEM, Identifier.fromNamespaceAndPath("test", "ruby"), new RubyItem(new Item.Properties().stacksTo(16).durability(250)));
    }

    static int ticks;

    @Override
    public void onInitializeClient() {
        // what a client mod does every tick: here, on the third one, it talks to the player
        ClientTickEvents.END_CLIENT_TICK.register(client -> {
            ticks++;
            if (ticks == 3) client.player.displayClientMessage(Component.literal("hello from the mod " + ticks), false);
        });
    }
}
