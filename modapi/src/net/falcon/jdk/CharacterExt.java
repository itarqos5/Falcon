package net.falcon.jdk;

public final class CharacterExt {
    private CharacterExt() {}

    public static int digit(char c, int radix) {
        int d = c >= '0' && c <= '9' ? c - '0' : (c >= 'a' && c <= 'z' ? c - 'a' + 10 : (c >= 'A' && c <= 'Z' ? c - 'A' + 10 : -1));
        return d >= 0 && d < radix ? d : -1;
    }
    public static boolean isLetterOrDigit(char c) { return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c > 127; }
    public static boolean isWhitespace(int c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f' || c == 0x0B; }
    public static boolean isISOControl(int c) { return c <= 0x1F || (c >= 0x7F && c <= 0x9F); }
    public static int charCount(int codePoint) { return codePoint >= 0x10000 ? 2 : 1; }
    public static int compare(char a, char b) { return a - b; }
    public static boolean isHighSurrogate(char c) { return c >= 0xD800 && c <= 0xDBFF; }
    public static char[] toChars(int codePoint) { return new char[] {(char) codePoint}; }
}
