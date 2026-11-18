package com.google.gson;

import java.io.IOException;
import java.io.Reader;

public final class JsonParser {
    private final String text;
    private int pos;

    private JsonParser(String text) { this.text = text; }

    public JsonParser() { this.text = ""; }

    public static JsonElement parseString(String json) {
        JsonParser p = new JsonParser(json);
        p.skip();
        JsonElement e = p.value();
        p.skip();
        if (p.pos < p.text.length()) throw new JsonSyntaxException("Unexpected text after the JSON value at " + p.pos);
        return e;
    }

    public static JsonElement parseReader(Reader reader) {
        StringBuilder sb = new StringBuilder();
        try {
            int c;
            while ((c = reader.read()) != -1) sb.append((char) c);
        } catch (IOException e) { throw new JsonParseException(e); }
        return parseString(sb.toString());
    }

    public JsonElement parse(String json) { return parseString(json); }
    public JsonElement parse(Reader reader) { return parseReader(reader); }

    private void skip() { while (pos < text.length() && (text.charAt(pos) == ' ' || text.charAt(pos) == '\n' || text.charAt(pos) == '\r' || text.charAt(pos) == '\t')) pos++; }

    private JsonElement value() {
        if (pos >= text.length()) throw new JsonSyntaxException("End of input");
        char c = text.charAt(pos);
        if (c == '{') {
            pos++;
            JsonObject o = new JsonObject();
            skip();
            if (text.charAt(pos) == '}') { pos++; return o; }
            while (true) {
                skip();
                if (text.charAt(pos) != '"') throw new JsonSyntaxException("Expected a name at " + pos);
                String name = string();
                skip();
                if (text.charAt(pos++) != ':') throw new JsonSyntaxException("Expected ':' at " + (pos - 1));
                skip();
                o.add(name, value());
                skip();
                char d = text.charAt(pos++);
                if (d == '}') return o;
                if (d != ',') throw new JsonSyntaxException("Expected ',' or '}' at " + (pos - 1));
            }
        }
        if (c == '[') {
            pos++;
            JsonArray a = new JsonArray();
            skip();
            if (text.charAt(pos) == ']') { pos++; return a; }
            while (true) {
                skip();
                a.add(value());
                skip();
                char d = text.charAt(pos++);
                if (d == ']') return a;
                if (d != ',') throw new JsonSyntaxException("Expected ',' or ']' at " + (pos - 1));
            }
        }
        if (c == '"') return new JsonPrimitive(string());
        if (text.startsWith("true", pos)) { pos += 4; return new JsonPrimitive(Boolean.TRUE); }
        if (text.startsWith("false", pos)) { pos += 5; return new JsonPrimitive(Boolean.FALSE); }
        if (text.startsWith("null", pos)) { pos += 4; return JsonNull.INSTANCE; }
        int start = pos;
        while (pos < text.length() && "+-0123456789.eE".indexOf(text.charAt(pos)) >= 0) pos++;
        if (start == pos) throw new JsonSyntaxException("Unexpected character '" + c + "' at " + pos);
        return new JsonPrimitive(new LazilyParsedNumber(text.substring(start, pos)));
    }

    private String string() {
        pos++;
        StringBuilder sb = new StringBuilder();
        while (true) {
            if (pos >= text.length()) throw new JsonSyntaxException("Unterminated string");
            char c = text.charAt(pos++);
            if (c == '"') return sb.toString();
            if (c == '\\') {
                char e = text.charAt(pos++);
                switch (e) {
                    case 'n': sb.append('\n'); break;
                    case 'r': sb.append('\r'); break;
                    case 't': sb.append('\t'); break;
                    case 'b': sb.append('\b'); break;
                    case 'f': sb.append('\f'); break;
                    case 'u': sb.append((char) Integer.parseInt(text.substring(pos, pos + 4), 16)); pos += 4; break;
                    default: sb.append(e);
                }
            } else {
                sb.append(c);
            }
        }
    }
}
