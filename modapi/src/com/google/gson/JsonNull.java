package com.google.gson;

public final class JsonNull extends JsonElement {
    public static final JsonNull INSTANCE = new JsonNull();

    @Override public JsonNull deepCopy() { return INSTANCE; }
    @Override public boolean equals(Object o) { return o instanceof JsonNull; }
    @Override public int hashCode() { return JsonNull.class.hashCode(); }
}
