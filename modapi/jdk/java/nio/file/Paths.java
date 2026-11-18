package java.nio.file;

public final class Paths {
    private Paths() {}

    public static Path get(String first, String... more) {
        StringBuilder sb = new StringBuilder(first == null ? "" : first.replace('\\', '/'));
        for (String m : more) {
            if (m == null || m.isEmpty()) continue;
            if (sb.length() > 0 && sb.charAt(sb.length() - 1) != '/') sb.append('/');
            sb.append(m.replace('\\', '/'));
        }
        return new net.falcon.FalconPath(sb.toString());
    }
}
