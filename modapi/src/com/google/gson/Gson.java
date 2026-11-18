package com.google.gson;

import java.io.IOException;
import java.io.Reader;
import java.lang.reflect.Type;
import java.util.ArrayList;
import java.util.Collection;
import java.util.HashMap;
import java.util.HashSet;
import java.util.LinkedHashMap;
import java.util.LinkedHashSet;
import java.util.List;
import java.util.Map;
import java.util.Set;
import net.falcon.Reflect;
import net.falcon.jdk.TypeImpl;

// JSON trees, the plain values that map onto them (Map, List, String, Number, Boolean, enums, arrays) and, by reflection on the fields (what the VM reports for a class), objects of any other class:
// every field that is not transient, by its name, as Gson does.
public final class Gson {
    private final boolean pretty, html, nulls;

    public Gson() { this(false, true, false); }
    Gson(boolean pretty, boolean html, boolean nulls) { this.pretty = pretty; this.html = html; this.nulls = nulls; }

    public String toJson(JsonElement e) { StringBuilder sb = new StringBuilder(); JsonWriterLite.write(e, sb, pretty ? "  " : null, "", html, nulls); return sb.toString(); }
    public void toJson(JsonElement e, Appendable out) { try { out.append(toJson(e)); } catch (IOException x) { throw new JsonParseException(x); } }
    public String toJson(Object o) { return toJson(toTree(o)); }
    public void toJson(Object o, Appendable out) { try { out.append(toJson(o)); } catch (IOException x) { throw new JsonParseException(x); } }
    public JsonElement toJsonTree(Object o) { return toTree(o); }

    // ---- an object as a tree ----

    @SuppressWarnings("unchecked")
    private JsonElement toTree(Object o) {
        if (o == null) return JsonNull.INSTANCE;
        if (o instanceof JsonElement e) return e;
        if (o instanceof String s) return new JsonPrimitive(s);
        if (o instanceof Number n) return new JsonPrimitive(n);
        if (o instanceof Boolean b) return new JsonPrimitive(b);
        if (o instanceof Character c) return new JsonPrimitive(c);
        if (o instanceof Enum<?> en) return new JsonPrimitive(en.name());
        if (o instanceof java.util.Optional<?> opt) return opt.isPresent() ? toTree(opt.get()) : JsonNull.INSTANCE;
        if (o instanceof java.util.UUID u) return new JsonPrimitive(u.toString());
        if (o instanceof Map<?, ?> m) {
            JsonObject out = new JsonObject();
            for (Map.Entry<?, ?> e : ((Map<Object, Object>) m).entrySet()) out.add(String.valueOf(e.getKey()), toTree(e.getValue()));
            return out;
        }
        if (o instanceof Iterable<?> it) {
            JsonArray out = new JsonArray();
            for (Object x : it) out.add(toTree(x));
            return out;
        }
        String cls = Reflect.className(o);
        if (cls.startsWith("[")) return arrayToTree(o, cls);
        JsonObject out = new JsonObject();
        for (String f : Reflect.fields(cls)) {
            String name = f.substring(0, f.indexOf('\u0000'));
            Object v = Reflect.get(o, name);
            if (v == null && !nulls) continue;
            out.add(name, toTree(v));
        }
        return out;
    }

    private JsonElement arrayToTree(Object o, String cls) {
        JsonArray out = new JsonArray();
        if (o instanceof Object[] a) { for (Object x : a) out.add(toTree(x)); }
        else if (o instanceof int[] a) { for (int x : a) out.add(Integer.valueOf(x)); }
        else if (o instanceof long[] a) { for (long x : a) out.add(Long.valueOf(x)); }
        else if (o instanceof double[] a) { for (double x : a) out.add(Double.valueOf(x)); }
        else if (o instanceof float[] a) { for (float x : a) out.add(Float.valueOf(x)); }
        else if (o instanceof boolean[] a) { for (boolean x : a) out.add(Boolean.valueOf(x)); }
        else if (o instanceof byte[] a) { for (byte x : a) out.add(Integer.valueOf(x)); }
        return out;
    }

    // ---- a tree as an object ----

    public <T> T fromJson(String json, Class<T> type) { return json == null ? null : fromJson(JsonParser.parseString(json), type); }
    public <T> T fromJson(String json, Type type) { return json == null ? null : fromJson(JsonParser.parseString(json), type); }
    public <T> T fromJson(Reader reader, Class<T> type) { return fromJson(JsonParser.parseReader(reader), type); }
    public <T> T fromJson(Reader reader, Type type) { return fromJson(JsonParser.parseReader(reader), type); }
    @SuppressWarnings("unchecked")
    public <T> T fromJson(JsonElement e, Class<T> type) { return (T) convert(e, TypeImpl.of(type.getName())); }
    @SuppressWarnings("unchecked")
    public <T> T fromJson(JsonElement e, Type type) { return (T) convert(e, (TypeImpl) type); }

    @SuppressWarnings("unchecked")
    private Object convert(JsonElement e, TypeImpl t) {
        String raw = t.raw;
        if (raw.equals("com.google.gson.JsonElement") || raw.equals("com.google.gson.JsonObject") || raw.equals("com.google.gson.JsonArray") || raw.equals("com.google.gson.JsonPrimitive")) return e;
        if (e == null || e.isJsonNull()) {
            switch (raw) {
                case "int": return Integer.valueOf(0);
                case "long": return Long.valueOf(0L);
                case "float": return Float.valueOf(0.0F);
                case "double": return Double.valueOf(0.0);
                case "boolean": return Boolean.FALSE;
                case "short": return Short.valueOf((short) 0);
                case "byte": return Byte.valueOf((byte) 0);
                case "char": return Character.valueOf((char) 0);
                default: return null;
            }
        }
        switch (raw) {
            case "java.lang.String": return e.getAsString();
            case "int": case "java.lang.Integer": return Integer.valueOf(e.getAsInt());
            case "long": case "java.lang.Long": return Long.valueOf(e.getAsLong());
            case "float": case "java.lang.Float": return Float.valueOf(e.getAsFloat());
            case "double": case "java.lang.Double": return Double.valueOf(e.getAsDouble());
            case "boolean": case "java.lang.Boolean": return Boolean.valueOf(e.getAsBoolean());
            case "short": case "java.lang.Short": return Short.valueOf((short) e.getAsInt());
            case "byte": case "java.lang.Byte": return Byte.valueOf((byte) e.getAsInt());
            case "char": case "java.lang.Character": return Character.valueOf(e.getAsString().charAt(0));
            case "java.util.UUID": return java.util.UUID.fromString(e.getAsString());
            case "java.lang.Object": return plain(e);
            default: break;
        }
        if (raw.startsWith("[")) {
            TypeImpl element = t.args[0];
            JsonArray a = e.getAsJsonArray();
            int n = a.size();
            switch (element.raw) {
                case "int": { int[] out = new int[n]; for (int i = 0; i < n; i++) out[i] = a.get(i).getAsInt(); return out; }
                case "long": { long[] out = new long[n]; for (int i = 0; i < n; i++) out[i] = a.get(i).getAsLong(); return out; }
                case "double": { double[] out = new double[n]; for (int i = 0; i < n; i++) out[i] = a.get(i).getAsDouble(); return out; }
                case "float": { float[] out = new float[n]; for (int i = 0; i < n; i++) out[i] = a.get(i).getAsFloat(); return out; }
                case "boolean": { boolean[] out = new boolean[n]; for (int i = 0; i < n; i++) out[i] = a.get(i).getAsBoolean(); return out; }
                default: { Object[] out = (Object[]) java.lang.reflect.Array.newInstance(Object.class, n); for (int i = 0; i < n; i++) out[i] = convert(a.get(i), element); return out; }
            }
        }
        if (raw.equals("java.util.List") || raw.equals("java.util.ArrayList") || raw.equals("java.util.Collection") || raw.equals("java.lang.Iterable") || raw.equals("java.util.LinkedList")) {
            List<Object> out = new ArrayList<>();
            TypeImpl element = t.args.length > 0 ? t.args[0] : TypeImpl.of("java/lang/Object");
            for (JsonElement x : e.getAsJsonArray()) out.add(convert(x, element));
            return out;
        }
        if (raw.equals("java.util.Set") || raw.equals("java.util.HashSet") || raw.equals("java.util.LinkedHashSet")) {
            Set<Object> out = new LinkedHashSet<>();
            TypeImpl element = t.args.length > 0 ? t.args[0] : TypeImpl.of("java/lang/Object");
            for (JsonElement x : e.getAsJsonArray()) out.add(convert(x, element));
            return out;
        }
        if (raw.equals("java.util.Map") || raw.equals("java.util.HashMap") || raw.equals("java.util.LinkedHashMap") || raw.equals("java.util.concurrent.ConcurrentHashMap")) {
            Map<Object, Object> out = new LinkedHashMap<>();
            TypeImpl value = t.args.length > 1 ? t.args[1] : TypeImpl.of("java/lang/Object");
            TypeImpl key = t.args.length > 0 ? t.args[0] : TypeImpl.of("java/lang/String");
            for (Map.Entry<String, JsonElement> en : e.getAsJsonObject().entrySet()) out.put(key.raw.equals("java.lang.String") ? en.getKey() : convert(new JsonPrimitive(en.getKey()), key), convert(en.getValue(), value));
            return out;
        }
        if (Reflect.isEnum(raw)) {
            Object constant = Reflect.enumValue(raw, e.getAsString());
            return constant;
        }
        // an object of the mod's own class: made by its no-argument constructor when it has one, then every field the JSON names is set
        JsonObject o = e.getAsJsonObject();
        Object instance = Reflect.newInstance(raw);
        if (instance == null) return null;
        for (String f : Reflect.fields(raw)) {
            String[] parts = f.split("\u0000", -1);
            JsonElement v = o.get(parts[0]);
            if (v == null) continue;
            TypeImpl ft = TypeImpl.parse(parts.length > 2 && !parts[2].isEmpty() ? parts[2] : parts[1]);
            Reflect.set(instance, parts[0], convert(v, ft));
        }
        return instance;
    }

    private Object plain(JsonElement e) {
        if (e.isJsonNull()) return null;
        if (e.isJsonPrimitive()) {
            JsonPrimitive p = e.getAsJsonPrimitive();
            return p.isBoolean() ? (Object) Boolean.valueOf(p.getAsBoolean()) : (p.isNumber() ? (Object) Double.valueOf(p.getAsDouble()) : (Object) p.getAsString());
        }
        if (e.isJsonArray()) { List<Object> out = new ArrayList<>(); for (JsonElement x : e.getAsJsonArray()) out.add(plain(x)); return out; }
        Map<String, Object> out = new LinkedHashMap<>();
        for (Map.Entry<String, JsonElement> en : e.getAsJsonObject().entrySet()) out.put(en.getKey(), plain(en.getValue()));
        return out;
    }
}
