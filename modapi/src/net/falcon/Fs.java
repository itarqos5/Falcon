package net.falcon;

// The files a mod may touch: the game's folder (read and write) and the mods folder (read). Every answer comes from the client (src/jvm/host_natives.cpp); a path outside them is as good as missing.
public final class Fs {
    private Fs() {}

    public static native boolean exists(String path);
    public static native boolean isDirectory(String path);
    public static native boolean isRegularFile(String path);
    public static native byte[] read(String path);
    public static native boolean write(String path, byte[] data, boolean append);
    public static native String[] list(String path);
    public static native boolean mkdirs(String path);
    public static native boolean delete(String path);
    public static native boolean move(String from, String to, boolean replace);
    public static native boolean copy(String from, String to, boolean replace);
    public static native long size(String path);
    public static native long lastModified(String path);
    public static native String absolute(String path);
}
