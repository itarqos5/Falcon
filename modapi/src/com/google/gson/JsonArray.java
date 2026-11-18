package com.google.gson;

import java.util.ArrayList;
import java.util.Iterator;
import java.util.List;

public final class JsonArray extends JsonElement implements Iterable<JsonElement> {
    private final List<JsonElement> elements = new ArrayList<>();

    public JsonArray() {}

    @Override public JsonArray deepCopy() { JsonArray c = new JsonArray(); for (JsonElement e : elements) c.add(e.deepCopy()); return c; }
    public void add(JsonElement e) { elements.add(e == null ? JsonNull.INSTANCE : e); }
    public void add(String s) { add(s == null ? JsonNull.INSTANCE : new JsonPrimitive(s)); }
    public void add(Number n) { add(n == null ? JsonNull.INSTANCE : new JsonPrimitive(n)); }
    public void add(Boolean b) { add(b == null ? JsonNull.INSTANCE : new JsonPrimitive(b)); }
    public void add(Character c) { add(c == null ? JsonNull.INSTANCE : new JsonPrimitive(c)); }
    public void addAll(JsonArray other) { elements.addAll(other.elements); }
    public JsonElement set(int i, JsonElement e) { return elements.set(i, e); }
    public JsonElement remove(int i) { return elements.remove(i); }
    public boolean remove(JsonElement e) { return elements.remove(e); }
    public boolean contains(JsonElement e) { return elements.contains(e); }
    public int size() { return elements.size(); }
    public boolean isEmpty() { return elements.isEmpty(); }
    public JsonElement get(int i) { return elements.get(i); }
    @Override public Iterator<JsonElement> iterator() { return elements.iterator(); }
    @Override public String getAsString() { return one().getAsString(); }
    @Override public int getAsInt() { return one().getAsInt(); }
    @Override public double getAsDouble() { return one().getAsDouble(); }
    @Override public boolean getAsBoolean() { return one().getAsBoolean(); }
    private JsonElement one() { if (elements.size() != 1) throw new IllegalStateException(); return elements.get(0); }
    @Override public boolean equals(Object o) { return o instanceof JsonArray a && a.elements.equals(elements); }
    @Override public int hashCode() { return elements.hashCode(); }
}
