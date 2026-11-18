package com.google.gson;

public abstract class JsonElement {
    public abstract JsonElement deepCopy();

    public boolean isJsonArray() { return this instanceof JsonArray; }
    public boolean isJsonObject() { return this instanceof JsonObject; }
    public boolean isJsonPrimitive() { return this instanceof JsonPrimitive; }
    public boolean isJsonNull() { return this instanceof JsonNull; }

    public JsonObject getAsJsonObject() { if (this instanceof JsonObject o) return o; throw new IllegalStateException("Not a JSON Object: " + this); }
    public JsonArray getAsJsonArray() { if (this instanceof JsonArray a) return a; throw new IllegalStateException("Not a JSON Array: " + this); }
    public JsonPrimitive getAsJsonPrimitive() { if (this instanceof JsonPrimitive p) return p; throw new IllegalStateException("Not a JSON Primitive: " + this); }
    public JsonNull getAsJsonNull() { if (this instanceof JsonNull n) return n; throw new IllegalStateException("Not a JSON Null: " + this); }

    public boolean getAsBoolean() { throw new UnsupportedOperationException(getClass().getSimpleName()); }
    public Number getAsNumber() { throw new UnsupportedOperationException(getClass().getSimpleName()); }
    public String getAsString() { throw new UnsupportedOperationException(getClass().getSimpleName()); }
    public double getAsDouble() { throw new UnsupportedOperationException(getClass().getSimpleName()); }
    public float getAsFloat() { throw new UnsupportedOperationException(getClass().getSimpleName()); }
    public long getAsLong() { throw new UnsupportedOperationException(getClass().getSimpleName()); }
    public int getAsInt() { throw new UnsupportedOperationException(getClass().getSimpleName()); }

    @Override public String toString() { StringBuilder sb = new StringBuilder(); JsonWriterLite.write(this, sb, null, ""); return sb.toString(); }
}
