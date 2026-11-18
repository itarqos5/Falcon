package net.falcon.jdk;

import java.util.ArrayList;
import java.util.Comparator;
import java.util.Iterator;
import java.util.List;
import java.util.Optional;
import java.util.function.BinaryOperator;
import java.util.function.Consumer;
import java.util.function.Function;
import java.util.function.IntFunction;
import java.util.function.Predicate;
import java.util.function.ToDoubleFunction;
import java.util.function.ToIntFunction;
import java.util.function.ToLongFunction;
import java.util.stream.Collector;
import java.util.stream.DoubleStream;
import java.util.stream.IntStream;
import java.util.stream.LongStream;
import java.util.stream.Stream;

public final class ListStream<T> implements Stream<T> {
    private final List<T> items;

    public ListStream(List<T> items) { this.items = items; }

    @Override public Stream<T> filter(Predicate<? super T> p) { List<T> out = new ArrayList<>(); for (T t : items) { if (p.test(t)) out.add(t); } return new ListStream<>(out); }
    @Override public <R> Stream<R> map(Function<? super T, ? extends R> f) { List<R> out = new ArrayList<>(); for (T t : items) out.add(f.apply(t)); return new ListStream<>(out); }
    @Override
    public <R> Stream<R> flatMap(Function<? super T, ? extends Stream<? extends R>> f) {
        List<R> out = new ArrayList<>();
        for (T t : items) { Stream<? extends R> s = f.apply(t); if (s != null) s.forEach(out::add); }
        return new ListStream<>(out);
    }
    @Override public IntStream mapToInt(ToIntFunction<? super T> f) { int[] a = new int[items.size()]; for (int i = 0; i < a.length; i++) a[i] = f.applyAsInt(items.get(i)); return new IntArrayStream(a); }
    @Override public LongStream mapToLong(ToLongFunction<? super T> f) { long s = 0; for (T t : items) s += f.applyAsLong(t); return new LongSum(s, items.size()); }
    @Override public DoubleStream mapToDouble(ToDoubleFunction<? super T> f) { double[] a = new double[items.size()]; for (int i = 0; i < a.length; i++) a[i] = f.applyAsDouble(items.get(i)); return new DoubleArrayStream(a); }
    @Override public Stream<T> distinct() { List<T> out = new ArrayList<>(); for (T t : items) { if (!out.contains(t)) out.add(t); } return new ListStream<>(out); }
    @Override public Stream<T> sorted() { List<T> out = new ArrayList<>(items); net.falcon.jdk.ListExt.sort(out, null); return new ListStream<>(out); }
    @Override public Stream<T> sorted(Comparator<? super T> c) { List<T> out = new ArrayList<>(items); net.falcon.jdk.ListExt.sort(out, c); return new ListStream<>(out); }
    @Override public Stream<T> peek(Consumer<? super T> c) { for (T t : items) c.accept(t); return this; }
    @Override public Stream<T> limit(long n) { List<T> out = new ArrayList<>(); for (int i = 0; i < items.size() && i < n; i++) out.add(items.get(i)); return new ListStream<>(out); }
    @Override public Stream<T> skip(long n) { List<T> out = new ArrayList<>(); for (int i = (int) n; i < items.size(); i++) out.add(items.get(i)); return new ListStream<>(out); }
    @Override public void forEach(Consumer<? super T> c) { for (T t : items) c.accept(t); }
    @Override public Object[] toArray() { Object[] a = new Object[items.size()]; for (int i = 0; i < a.length; i++) a[i] = items.get(i); return a; }
    @Override public <A> A[] toArray(IntFunction<A[]> generator) { A[] a = generator.apply(items.size()); for (int i = 0; i < a.length; i++) a[i] = (A) items.get(i); return a; }
    @Override public T reduce(T identity, BinaryOperator<T> op) { T r = identity; for (T t : items) r = op.apply(r, t); return r; }
    @Override
    public Optional<T> reduce(BinaryOperator<T> op) {
        if (items.isEmpty()) return Optional.empty();
        T r = items.get(0);
        for (int i = 1; i < items.size(); i++) r = op.apply(r, items.get(i));
        return Optional.of(r);
    }
    @Override
    public <R, A> R collect(Collector<? super T, A, R> collector) {
        A acc = collector.supplier().get();
        java.util.function.BiConsumer<A, ? super T> accumulate = collector.accumulator();
        for (T t : items) ((java.util.function.BiConsumer<A, T>) accumulate).accept(acc, t);
        return collector.finisher().apply(acc);
    }
    @Override public List<T> toList() { return new ArrayList<>(items); }
    @Override public Optional<T> min(Comparator<? super T> c) { if (items.isEmpty()) return Optional.empty(); T m = items.get(0); for (T t : items) { if (c.compare(t, m) < 0) m = t; } return Optional.of(m); }
    @Override public Optional<T> max(Comparator<? super T> c) { if (items.isEmpty()) return Optional.empty(); T m = items.get(0); for (T t : items) { if (c.compare(t, m) > 0) m = t; } return Optional.of(m); }
    @Override public long count() { return items.size(); }
    @Override public boolean anyMatch(Predicate<? super T> p) { for (T t : items) { if (p.test(t)) return true; } return false; }
    @Override public boolean allMatch(Predicate<? super T> p) { for (T t : items) { if (!p.test(t)) return false; } return true; }
    @Override public boolean noneMatch(Predicate<? super T> p) { return !anyMatch(p); }
    @Override public Optional<T> findFirst() { return items.isEmpty() ? Optional.empty() : Optional.of(items.get(0)); }
    @Override public Optional<T> findAny() { return findFirst(); }
    @Override public Iterator<T> iterator() { return items.iterator(); }
    @Override public void close() {}

    private static final class LongSum implements LongStream {
        private final long sum, count;
        LongSum(long sum, long count) { this.sum = sum; this.count = count; }
        @Override public long sum() { return sum; }
        @Override public long count() { return count; }
    }
}
