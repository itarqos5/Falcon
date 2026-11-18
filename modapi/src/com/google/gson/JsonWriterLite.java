package com.google.gson;

import java.util.Map;

// JSON text of a tree, the way Gson writes it: compact, or with `indent` per level.
final class JsonWriterLite {
    private JsonWriterLite() {}

    static void quote(String s, StringBuilder out, boolean htmlSafe) {
        out.append('"');
        for (int i = 0; i < s.length(); i++) {
            char c = s.charAt(i);
            switch (c) {
                case '"': out.append("\\\""); break;
                case '\\': out.append("\\\\"); break;
                case '\n': out.append("\\n"); break;
                case '\r': out.append("\\r"); break;
                case '\t': out.append("\\t"); break;
                case '\b': out.append("\\b"); break;
                case '\f': out.append("\\f"); break;
                default:
                    if (c < ' ' || (htmlSafe && (c == '<' || c == '>' || c == '&' || c == '=' || c == '\''))) {
                        String h = Integer.toHexString(c);
                        out.append("\\u");
                        for (int k = h.length(); k < 4; k++) out.append('0');
                        out.append(h);
                    } else {
                        out.append(c);
                    }
            }
        }
        out.append('"');
    }

    static void write(JsonElement e, StringBuilder out, String indent, String current) { write(e, out, indent, current, false, false); }

    static void write(JsonElement e, StringBuilder out, String indent, String current, boolean htmlSafe, boolean serializeNulls) {
        if (e == null || e.isJsonNull()) {
            out.append("null");
        } else if (e.isJsonPrimitive()) {
            JsonPrimitive p = e.getAsJsonPrimitive();
            if (p.isString()) quote(p.getAsString(), out, htmlSafe); else out.append(p.getAsString());
        } else if (e.isJsonArray()) {
            JsonArray a = e.getAsJsonArray();
            if (a.size() == 0) { out.append("[]"); return; }
            String inner = indent == null ? null : current + indent;
            out.append('[');
            boolean first = true;
            for (JsonElement x : a) {
                if (!first) out.append(',');
                first = false;
                if (indent != null) out.append('\n').append(inner);
                write(x, out, indent, inner, htmlSafe, serializeNulls);
            }
            if (indent != null) out.append('\n').append(current);
            out.append(']');
        } else {
            JsonObject o = e.getAsJsonObject();
            String inner = indent == null ? null : current + indent;
            boolean first = true;
            out.append('{');
            for (Map.Entry<String, JsonElement> m : o.entrySet()) {
                if (m.getValue().isJsonNull() && !serializeNulls) continue;
                if (!first) out.append(',');
                first = false;
                if (indent != null) out.append('\n').append(inner);
                quote(m.getKey(), out, htmlSafe);
                out.append(indent != null ? ": " : ":");
                write(m.getValue(), out, indent, inner, htmlSafe, serializeNulls);
            }
            if (!first && indent != null) out.append('\n').append(current);
            out.append('}');
        }
    }
}
