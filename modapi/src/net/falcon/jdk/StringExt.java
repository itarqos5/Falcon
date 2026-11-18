package net.falcon.jdk;

import java.util.ArrayList;
import java.util.List;
import java.util.Locale;
import java.util.regex.Matcher;
import java.util.regex.Pattern;
import java.util.stream.IntStream;
import java.util.stream.Stream;

public final class StringExt {
    private StringExt() {}

    public static boolean matches(String s, String regex) { return Pattern.compile(regex).matcher(s).matches(); }
    public static String replaceAll(String s, String regex, String replacement) { return Pattern.compile(regex).matcher(s).replaceAll(replacement); }
    public static String replaceFirst(String s, String regex, String replacement) { return Pattern.compile(regex).matcher(s).replaceFirst(replacement); }
    public static String[] split(String s, String regex, int limit) { return Pattern.compile(regex).split(s, limit); }
    public static int codePointAt(String s, int i) { return s.charAt(i); }
    public static int codePointCount(String s, int begin, int end) { return end - begin; }
    public static int offsetByCodePoints(String s, int index, int offset) { return index + offset; }
    public static IntStream chars(String s) { int[] a = new int[s.length()]; for (int i = 0; i < a.length; i++) a[i] = s.charAt(i); return IntStream.of(a); }
    public static IntStream codePoints(String s) { return chars(s); }
    public static Stream<String> lines(String s) { List<String> l = new ArrayList<>(); for (String line : s.split("\n", -1)) l.add(line.endsWith("\r") ? line.substring(0, line.length() - 1) : line); if (s.endsWith("\n")) l.remove(l.size() - 1); return l.stream(); }
    public static String format(Locale locale, String format, Object... args) { return String.format(format, args); }
    public static int indexOf(String s, String what, int from) { for (int i = Math.max(0, from); i + what.length() <= s.length(); i++) { if (s.startsWith(what, i)) return i; } return -1; }
    public static int lastIndexOf(String s, String what) { for (int i = s.length() - what.length(); i >= 0; i--) { if (s.startsWith(what, i)) return i; } return -1; }
    public static boolean startsWith(String s, String prefix, int offset) { return offset >= 0 && offset + prefix.length() <= s.length() && s.substring(offset, offset + prefix.length()).equals(prefix); }
    public static boolean regionMatches(String s, boolean ignoreCase, int toffset, String other, int ooffset, int len) {
        if (toffset < 0 || ooffset < 0 || toffset + len > s.length() || ooffset + len > other.length()) return false;
        String a = s.substring(toffset, toffset + len), b = other.substring(ooffset, ooffset + len);
        return ignoreCase ? a.equalsIgnoreCase(b) : a.equals(b);
    }
    public static String stripLeading(String s) { int i = 0; while (i < s.length() && s.charAt(i) <= ' ') i++; return s.substring(i); }
    public static String stripTrailing(String s) { int e = s.length(); while (e > 0 && s.charAt(e - 1) <= ' ') e--; return s.substring(0, e); }
    public static String formatted(String s, Object... args) { return String.format(s, args); }
    public static String valueOf(char[] chars) { return new String(chars); }
    public static String join(CharSequence delimiter, Iterable<? extends CharSequence> items) { StringBuilder sb = new StringBuilder(); boolean first = true; for (CharSequence c : items) { if (!first) sb.append(delimiter); first = false; sb.append(c); } return sb.toString(); }
}
