package java.util.stream;

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

// Streams here are lists with a pipeline's methods: every step is done when it is called (the VM has one thread, and what the mods do with them is small).
public interface Stream<T> extends AutoCloseable {
    Stream<T> filter(Predicate<? super T> p);
    <R> Stream<R> map(Function<? super T, ? extends R> f);
    <R> Stream<R> flatMap(Function<? super T, ? extends Stream<? extends R>> f);
    IntStream mapToInt(ToIntFunction<? super T> f);
    LongStream mapToLong(ToLongFunction<? super T> f);
    DoubleStream mapToDouble(ToDoubleFunction<? super T> f);
    Stream<T> distinct();
    Stream<T> sorted();
    Stream<T> sorted(Comparator<? super T> c);
    Stream<T> peek(Consumer<? super T> c);
    Stream<T> limit(long n);
    Stream<T> skip(long n);
    void forEach(Consumer<? super T> c);
    Object[] toArray();
    <A> A[] toArray(IntFunction<A[]> generator);
    T reduce(T identity, BinaryOperator<T> op);
    Optional<T> reduce(BinaryOperator<T> op);
    <R, A> R collect(Collector<? super T, A, R> collector);
    List<T> toList();
    Optional<T> min(Comparator<? super T> c);
    Optional<T> max(Comparator<? super T> c);
    long count();
    boolean anyMatch(Predicate<? super T> p);
    boolean allMatch(Predicate<? super T> p);
    boolean noneMatch(Predicate<? super T> p);
    Optional<T> findFirst();
    Optional<T> findAny();
    Iterator<T> iterator();
    @Override void close();

    static <T> Stream<T> empty() { return new net.falcon.jdk.ListStream<>(new ArrayList<>()); }
    @SafeVarargs static <T> Stream<T> of(T... values) { List<T> l = new ArrayList<>(); for (T v : values) l.add(v); return new net.falcon.jdk.ListStream<>(l); }
    static <T> Stream<T> ofNullable(T t) { return t == null ? empty() : of(t); }
    static <T> Stream<T> concat(Stream<? extends T> a, Stream<? extends T> b) {
        List<T> l = new ArrayList<>();
        a.forEach(l::add);
        b.forEach(l::add);
        return new net.falcon.jdk.ListStream<>(l);
    }
    static <T> Stream<T> iterate(T seed, java.util.function.UnaryOperator<T> next, int limitHint) { List<T> l = new ArrayList<>(); T v = seed; for (int i = 0; i < limitHint; i++) { l.add(v); v = next.apply(v); } return new net.falcon.jdk.ListStream<>(l); }
}
