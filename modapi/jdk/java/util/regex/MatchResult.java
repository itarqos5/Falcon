package java.util.regex;

public interface MatchResult {
    int start();
    int end();
    String group();
    String group(int group);
}
