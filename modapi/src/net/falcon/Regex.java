package net.falcon;

// Regular expressions, answered by the client (std::regex, ECMAScript syntax: what Java's patterns have in common with it).
public final class Regex {
    private Regex() {}

    // A handle for a pattern (flags: 1 case insensitive, 2 multiline); -1 when the pattern is not valid.
    public static native int compile(String pattern, int flags);
    // The groups of the first match at or after `from` (anchored: the whole text must match): start and end of each, -1 for a group that did not take part; null when there is none.
    public static native int[] find(int handle, String text, int from, boolean anchored);
}
