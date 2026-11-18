package java.util.concurrent;

import java.util.ArrayList;
import java.util.List;
import java.util.function.BiConsumer;
import java.util.function.BiFunction;
import java.util.function.Consumer;
import java.util.function.Function;
import java.util.function.Supplier;

// A future on one thread: what is supplied is computed at once, and what follows a result runs when it is there.
public class CompletableFuture<T> {
    private boolean done, cancelled;
    private T value;
    private Throwable failure;
    private final List<Runnable> waiting = new ArrayList<>();

    public CompletableFuture() {}

    public static <U> CompletableFuture<U> completedFuture(U v) { CompletableFuture<U> f = new CompletableFuture<>(); f.complete(v); return f; }
    public static <U> CompletableFuture<U> failedFuture(Throwable t) { CompletableFuture<U> f = new CompletableFuture<>(); f.completeExceptionally(t); return f; }
    public static <U> CompletableFuture<U> supplyAsync(Supplier<U> s) { return supplyAsync(s, null); }
    public static <U> CompletableFuture<U> supplyAsync(Supplier<U> s, Executor e) {
        CompletableFuture<U> f = new CompletableFuture<>();
        try { f.complete(s.get()); } catch (Throwable t) { f.completeExceptionally(t); }
        return f;
    }
    public static CompletableFuture<Void> runAsync(Runnable r) { return runAsync(r, null); }
    public static CompletableFuture<Void> runAsync(Runnable r, Executor e) {
        CompletableFuture<Void> f = new CompletableFuture<>();
        try { r.run(); f.complete(null); } catch (Throwable t) { f.completeExceptionally(t); }
        return f;
    }
    public static CompletableFuture<Void> allOf(CompletableFuture<?>... futures) { CompletableFuture<Void> f = new CompletableFuture<>(); f.complete(null); return f; }
    public static Executor delayedExecutor(long delay, TimeUnit unit) { return r -> r.run(); }

    public boolean complete(T v) { if (done) return false; value = v; done = true; fire(); return true; }
    public boolean completeExceptionally(Throwable t) { if (done) return false; failure = t; done = true; fire(); return true; }
    public boolean cancel(boolean mayInterrupt) { if (done) return false; cancelled = true; failure = new CancellationException(); done = true; fire(); return true; }
    public boolean isDone() { return done; }
    public boolean isCancelled() { return cancelled; }
    public boolean isCompletedExceptionally() { return done && failure != null; }
    public T getNow(T absent) { if (!done) return absent; return join(); }
    public CompletableFuture<T> orTimeout(long t, TimeUnit u) { return this; }

    public T join() {
        if (failure != null) throw failure instanceof CompletionException c ? c : new CompletionException(failure);
        return value;
    }
    public T get() throws InterruptedException, ExecutionException { if (failure != null) throw new ExecutionException(failure); return value; }
    public T get(long t, TimeUnit u) throws InterruptedException, ExecutionException, TimeoutException { return get(); }

    private void fire() { List<Runnable> run = new ArrayList<>(waiting); waiting.clear(); for (Runnable r : run) r.run(); }
    private void then(Runnable r) { if (done) r.run(); else waiting.add(r); }
    private static Throwable unwrap(Throwable t) { return t instanceof CompletionException && t.getCause() != null ? t.getCause() : t; }

    public <U> CompletableFuture<U> thenApply(Function<? super T, ? extends U> fn) {
        CompletableFuture<U> out = new CompletableFuture<>();
        then(() -> { if (failure != null) out.completeExceptionally(failure); else { try { out.complete(fn.apply(value)); } catch (Throwable t) { out.completeExceptionally(t); } } });
        return out;
    }
    public <U> CompletableFuture<U> thenApplyAsync(Function<? super T, ? extends U> fn) { return thenApply(fn); }
    public CompletableFuture<Void> thenAccept(Consumer<? super T> c) { return thenApply(v -> { c.accept(v); return null; }); }
    public CompletableFuture<Void> thenRun(Runnable r) { return thenApply(v -> { r.run(); return null; }); }
    public CompletableFuture<Void> thenRunAsync(Runnable r, Executor e) { return thenRun(r); }
    public <U> CompletableFuture<U> thenCompose(Function<? super T, ? extends CompletableFuture<U>> fn) {
        CompletableFuture<U> out = new CompletableFuture<>();
        then(() -> {
            if (failure != null) { out.completeExceptionally(failure); return; }
            try {
                CompletableFuture<U> inner = fn.apply(value);
                inner.then(() -> { if (inner.failure != null) out.completeExceptionally(inner.failure); else out.complete(inner.value); });
            } catch (Throwable t) { out.completeExceptionally(t); }
        });
        return out;
    }
    public CompletableFuture<T> exceptionally(Function<Throwable, ? extends T> fn) {
        CompletableFuture<T> out = new CompletableFuture<>();
        then(() -> { if (failure == null) out.complete(value); else { try { out.complete(fn.apply(failure)); } catch (Throwable t) { out.completeExceptionally(t); } } });
        return out;
    }
    public <U> CompletableFuture<U> handle(BiFunction<? super T, Throwable, ? extends U> fn) {
        CompletableFuture<U> out = new CompletableFuture<>();
        then(() -> { try { out.complete(fn.apply(value, failure)); } catch (Throwable t) { out.completeExceptionally(t); } });
        return out;
    }
    public CompletableFuture<T> whenComplete(BiConsumer<? super T, ? super Throwable> action) {
        CompletableFuture<T> out = new CompletableFuture<>();
        then(() -> {
            try { action.accept(value, failure); } catch (Throwable t) { if (failure == null) { out.completeExceptionally(t); return; } }
            if (failure != null) out.completeExceptionally(failure); else out.complete(value);
        });
        return out;
    }
    public CompletableFuture<T> whenCompleteAsync(BiConsumer<? super T, ? super Throwable> action, Executor e) { return whenComplete(action); }
}
