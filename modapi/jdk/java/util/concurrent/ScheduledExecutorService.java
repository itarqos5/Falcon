package java.util.concurrent;

public class ScheduledExecutorService extends ExecutorService {
    public CompletableFuture<Void> schedule(Runnable task, long delay, TimeUnit unit) { return CompletableFuture.runAsync(task); }
}
