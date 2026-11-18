package java.util.concurrent;

public final class Executors {
    private Executors() {}

    public static ExecutorService newSingleThreadExecutor() { return new ExecutorService(); }
    public static ExecutorService newSingleThreadExecutor(Object factory) { return new ExecutorService(); }
    public static ExecutorService newFixedThreadPool(int n) { return new ExecutorService(); }
    public static ExecutorService newCachedThreadPool() { return new ExecutorService(); }
    public static ScheduledExecutorService newSingleThreadScheduledExecutor() { return new ScheduledExecutorService(); }
}
