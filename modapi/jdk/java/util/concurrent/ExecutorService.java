package java.util.concurrent;

// One thread: what is submitted runs at once.
public class ExecutorService implements Executor, AutoCloseable {
    private boolean shut;

    @Override public void execute(Runnable command) { if (!shut) command.run(); }
    public <T> CompletableFuture<T> submit(java.util.function.Supplier<T> task) { return CompletableFuture.supplyAsync(task); }
    public CompletableFuture<Void> submit(Runnable task) { return CompletableFuture.runAsync(task); }
    public void shutdown() { shut = true; }
    public java.util.List<Runnable> shutdownNow() { shut = true; return new java.util.ArrayList<>(); }
    public boolean isShutdown() { return shut; }
    public boolean awaitTermination(long t, TimeUnit u) { return true; }
    @Override public void close() { shut = true; }
}
