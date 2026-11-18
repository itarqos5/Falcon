package com.google.gson;

import java.util.LinkedHashMap;
import java.util.Map;
import java.util.Set;

public final class JsonObject extends JsonElement {
    private final Map<String, JsonElement> members = new LinkedHashMap<>();

    public JsonObject() {}

    @Override public JsonObject deepCopy() { JsonObject c = new JsonObject(); for (Map.Entry<String, JsonElement> e : members.entrySet()) c.add(e.getKey(), e.getValue().deepCopy()); return c; }
    public void add(String name, JsonElement value) { members.put(name, value == null ? JsonNull.INSTANCE : value); }
    public JsonElement remove(String name) { return members.remove(name); }
    public void addProperty(String name, String v) { add(name, v == null ? JsonNull.INSTANCE : new JsonPrimitive(v)); }
    public void addProperty(String name, Number v) { add(name, v == null ? JsonNull.INSTANCE : new JsonPrimitive(v)); }
    public void addProperty(String name, Boolean v) { add(name, v == null ? JsonNull.INSTANCE : new JsonPrimitive(v)); }
    public void addProperty(String name, Character v) { add(name, v == null ? JsonNull.INSTANCE : new JsonPrimitive(v)); }
    public Set<Map.Entry<String, JsonElement>> entrySet() { return members.entrySet(); }
    public Set<String> keySet() { return members.keySet(); }
    public int size() { return members.size(); }
    public boolean isEmpty() { return members.isEmpty(); }
    public boolean has(String name) { return members.containsKey(name); }
    public JsonElement get(String name) { return members.get(name); }
    public JsonPrimitive getAsJsonPrimitive(String name) { return (JsonPrimitive) members.get(name); }
    public JsonArray getAsJsonArray(String name) { return (JsonArray) members.get(name); }
    public JsonObject getAsJsonObject(String name) { return (JsonObject) members.get(name); }
    public Map<String, JsonElement> asMap() { return members; }
    @Override public boolean equals(Object o) { return o instanceof JsonObject j && j.members.equals(members); }
    @Override public int hashCode() { return members.hashCode(); }
}
