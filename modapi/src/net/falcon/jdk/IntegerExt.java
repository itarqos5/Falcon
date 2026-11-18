package net.falcon.jdk;

public final class IntegerExt {
    private IntegerExt() {}

    public static int bitCount(int i) { int c = 0; int x = i; while (x != 0) { c += x & 1; x >>>= 1; } return c; }
    public static int signum(int i) { return i > 0 ? 1 : (i < 0 ? -1 : 0); }
    public static int sum(int a, int b) { return a + b; }
    public static int parseInt(String s, int radix) {
        boolean neg = s.startsWith("-");
        int i = neg || s.startsWith("+") ? 1 : 0;
        if (i >= s.length()) throw new NumberFormatException("For input string: \"" + s + "\"");
        int r = 0;
        for (; i < s.length(); i++) {
            int d = CharacterExt.digit(s.charAt(i), radix);
            if (d < 0) throw new NumberFormatException("For input string: \"" + s + "\"");
            r = r * radix + d;
        }
        return neg ? -r : r;
    }
}
