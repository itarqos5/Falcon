package com.google.gson;

public final class JsonPrimitive extends JsonElement {
    private final Object value;  // Boolean, Number or String

    public JsonPrimitive(Boolean b) { value = b; }
    public JsonPrimitive(Number n) { value = n; }
    public JsonPrimitive(String s) { value = s; }
    public JsonPrimitive(Character c) { value = String.valueOf(c); }

    @Override public JsonPrimitive deepCopy() { return this; }
    public boolean isBoolean() { return value instanceof Boolean; }
    public boolean isNumber() { return value instanceof Number; }
    public boolean isString() { return value instanceof String; }

    @Override public boolean getAsBoolean() { return value instanceof Boolean b ? b : Boolean.parseBoolean(getAsString()); }
    @Override public Number getAsNumber() { return value instanceof Number n ? n : new LazilyParsedNumber((String) value); }
    @Override public String getAsString() { return value instanceof Number n ? n.toString() : (value instanceof Boolean b ? b.toString() : (String) value); }
    @Override public double getAsDouble() { return value instanceof Number n ? n.doubleValue() : Double.parseDouble(getAsString()); }
    @Override public float getAsFloat() { return value instanceof Number n ? n.floatValue() : Float.parseFloat(getAsString()); }
    @Override public long getAsLong() { return value instanceof Number n ? n.longValue() : Long.parseLong(getAsString()); }
    @Override public int getAsInt() { return value instanceof Number n ? n.intValue() : Integer.parseInt(getAsString()); }
    @Override public boolean equals(Object o) { return o instanceof JsonPrimitive p && (value instanceof Number && p.value instanceof Number ? getAsDouble() == p.getAsDouble() : value.equals(p.value)); }
    @Override public int hashCode() { return value.hashCode(); }
}
