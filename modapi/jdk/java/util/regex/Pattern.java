package java.util.regex;

import java.util.ArrayList;
import java.util.List;

public final class Pattern {
    public static final int CASE_INSENSITIVE = 2;
    public static final int MULTILINE = 8;
    public static final int DOTALL = 32;
    private final String pattern;
    private final int flags;
    final int handle;

    private Pattern(String source, int flagBits) {
        String text = source;
        int bits = flagBits;
        // inline flags at the start, (?i) (?m) (?s) (?is): they become flags
        while (text.startsWith("(?") && text.indexOf(')') > 2) {
            String f = text.substring(2, text.indexOf(')'));
            boolean only = true;
            for (int i = 0; i < f.length(); i++) {
                char c = f.charAt(i);
                if (c != 'i' && c != 'm' && c != 's') only = false;
            }
            if (!only) break;
            if (f.indexOf('i') >= 0) bits |= CASE_INSENSITIVE;
            if (f.indexOf('m') >= 0) bits |= MULTILINE;
            text = text.substring(text.indexOf(')') + 1);
        }
        text = translate(text);
        this.pattern = source;
        this.flags = bits;
        this.handle = net.falcon.Regex.compile(text, ((bits & CASE_INSENSITIVE) != 0 ? 1 : 0) | ((bits & MULTILINE) != 0 ? 2 : 0));
        if (handle < 0) throw new PatternSyntaxException("Invalid pattern", source);
    }

    // Java's classes of characters, spelled the way ECMAScript has them.
    private static String translate(String p) {
        String[] names = {"\\p{L}", "\\p{Alpha}", "\\p{Lu}", "\\p{Upper}", "\\p{Ll}", "\\p{Lower}", "\\p{Digit}", "\\p{N}", "\\p{Alnum}", "\\p{Punct}", "\\p{Space}", "\\p{IsAlphabetic}"};
        String[] sets = {"A-Za-zÀ-ɏ", "A-Za-z", "A-Z", "A-Z", "a-z", "a-z", "0-9", "0-9", "A-Za-z0-9", "!-/:-@\\[-`{-~", " \\t\\n\\r\\f", "A-Za-z"};
        String out = p;
        for (int n = 0; n < names.length; n++) {
            int i;
            while ((i = out.indexOf(names[n])) >= 0) {
                boolean inClass = false;
                for (int k = 0; k < i; k++) {
                    char c = out.charAt(k);
                    if (c == '\\') { k++; continue; }
                    if (c == '[') inClass = true; else if (c == ']') inClass = false;
                }
                out = out.substring(0, i) + (inClass ? sets[n] : "[" + sets[n] + "]") + out.substring(i + names[n].length());
            }
        }
        return out;
    }

    public static Pattern compile(String regex) { return new Pattern(regex, 0); }
    public static Pattern compile(String regex, int flags) { return new Pattern(regex, flags); }
    public static boolean matches(String regex, CharSequence input) { return compile(regex).matcher(input).matches(); }

    public static String quote(String s) {
        StringBuilder sb = new StringBuilder();
        for (int i = 0; i < s.length(); i++) {
            char c = s.charAt(i);
            if ("\\^$.|?*+()[]{}".indexOf(c) >= 0) sb.append('\\');
            sb.append(c);
        }
        return sb.toString();
    }

    public Matcher matcher(CharSequence input) { return new Matcher(this, String.valueOf(input)); }
    public String pattern() { return pattern; }
    public int flags() { return flags; }

    public String[] split(CharSequence input, int limit) {
        String text = String.valueOf(input);
        List<String> parts = new ArrayList<>();
        Matcher m = matcher(text);
        int last = 0;
        while (m.find()) {
            if (limit > 0 && parts.size() == limit - 1) break;
            if (m.end() == 0) continue;
            if (m.start() == m.end() && m.start() >= text.length()) break;
            parts.add(text.substring(last, m.start()));
            last = m.end();
        }
        parts.add(text.substring(last));
        if (limit == 0) {
            while (parts.size() > 1 && parts.get(parts.size() - 1).isEmpty()) parts.remove(parts.size() - 1);
            if (parts.size() == 1 && parts.get(0).isEmpty() && !text.isEmpty()) parts.remove(0);
        }
        return parts.toArray(new String[0]);
    }

    public String[] split(CharSequence input) { return split(input, 0); }
    @Override public String toString() { return pattern; }
}
