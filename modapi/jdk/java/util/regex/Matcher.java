package java.util.regex;

public final class Matcher implements MatchResult {
    private final Pattern pattern;
    private String text;
    private int[] groups;
    private int from;

    Matcher(Pattern pattern, String text) { this.pattern = pattern; this.text = text; }

    public boolean matches() { groups = net.falcon.Regex.find(pattern.handle, text, 0, true); return groups != null; }
    public boolean find() {
        if (from > text.length()) { groups = null; return false; }
        groups = net.falcon.Regex.find(pattern.handle, text, from, false);
        if (groups == null) { from = text.length() + 1; return false; }
        from = groups[1] == groups[0] ? groups[1] + 1 : groups[1];
        return true;
    }
    public boolean find(int start) { from = start; return find(); }
    public boolean lookingAt() { groups = net.falcon.Regex.find(pattern.handle, text, 0, false); if (groups != null && groups[0] != 0) groups = null; return groups != null; }
    public Matcher reset() { from = 0; groups = null; return this; }
    public Matcher reset(CharSequence input) { text = String.valueOf(input); return reset(); }

    private void check() { if (groups == null) throw new IllegalStateException("No match found"); }
    @Override public int start() { check(); return groups[0]; }
    @Override public int end() { check(); return groups[1]; }
    public int start(int g) { check(); return groups[g * 2]; }
    public int end(int g) { check(); return groups[g * 2 + 1]; }
    public int groupCount() { return groups == null ? 0 : groups.length / 2 - 1; }
    @Override public String group() { return group(0); }
    @Override public String group(int g) { check(); if (g * 2 >= groups.length) throw new IndexOutOfBoundsException("No group " + g); return groups[g * 2] < 0 ? null : text.substring(groups[g * 2], groups[g * 2 + 1]); }

    private String expand(String replacement) {
        StringBuilder sb = new StringBuilder();
        for (int i = 0; i < replacement.length(); i++) {
            char c = replacement.charAt(i);
            if (c == '\\' && i + 1 < replacement.length()) { sb.append(replacement.charAt(++i)); }
            else if (c == '$' && i + 1 < replacement.length() && Character.isDigit(replacement.charAt(i + 1))) { String g = group(replacement.charAt(++i) - '0'); if (g != null) sb.append(g); }
            else sb.append(c);
        }
        return sb.toString();
    }

    public String replaceAll(String replacement) {
        reset();
        StringBuilder sb = new StringBuilder();
        int last = 0;
        while (find()) { sb.append(text, last, start()); sb.append(expand(replacement)); last = end(); }
        sb.append(text.substring(last));
        return sb.toString();
    }

    public String replaceFirst(String replacement) {
        reset();
        if (!find()) return text;
        return text.substring(0, start()) + expand(replacement) + text.substring(end());
    }

    public static String quoteReplacement(String s) { return s.replace("\\", "\\\\").replace("$", "\\$"); }
}
