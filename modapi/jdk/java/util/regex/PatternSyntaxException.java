package java.util.regex;

public class PatternSyntaxException extends IllegalArgumentException {
    public PatternSyntaxException(String desc, String regex) { super(desc + ": " + regex); }
}
