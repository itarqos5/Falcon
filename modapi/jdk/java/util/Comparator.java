package java.util;

import java.util.function.Function;
import java.util.function.ToDoubleFunction;
import java.util.function.ToIntFunction;
import java.util.function.ToLongFunction;

@FunctionalInterface
public interface Comparator<T> {
    int compare(T a, T b);

    default Comparator<T> reversed() { return (a, b) -> compare(b, a); }
    default Comparator<T> thenComparing(Comparator<? super T> other) { return (a, b) -> { int r = compare(a, b); return r != 0 ? r : other.compare(a, b); }; }
    default <U extends Comparable<? super U>> Comparator<T> thenComparing(Function<? super T, ? extends U> key) { return thenComparing(comparing(key)); }
    default <U> Comparator<T> thenComparing(Function<? super T, ? extends U> key, Comparator<? super U> cmp) { return thenComparing(comparing(key, cmp)); }
    default Comparator<T> thenComparingInt(ToIntFunction<? super T> key) { return thenComparing(comparingInt(key)); }
    default Comparator<T> thenComparingLong(ToLongFunction<? super T> key) { return thenComparing(comparingLong(key)); }
    default Comparator<T> thenComparingDouble(ToDoubleFunction<? super T> key) { return thenComparing(comparingDouble(key)); }

    static <T, U extends Comparable<? super U>> Comparator<T> comparing(Function<? super T, ? extends U> key) { return (a, b) -> key.apply(a).compareTo(key.apply(b)); }
    static <T, U> Comparator<T> comparing(Function<? super T, ? extends U> key, Comparator<? super U> cmp) { return (a, b) -> cmp.compare(key.apply(a), key.apply(b)); }
    static <T> Comparator<T> comparingInt(ToIntFunction<? super T> key) { return (a, b) -> Integer.compare(key.applyAsInt(a), key.applyAsInt(b)); }
    static <T> Comparator<T> comparingLong(ToLongFunction<? super T> key) { return (a, b) -> Long.compare(key.applyAsLong(a), key.applyAsLong(b)); }
    static <T> Comparator<T> comparingDouble(ToDoubleFunction<? super T> key) { return (a, b) -> Double.compare(key.applyAsDouble(a), key.applyAsDouble(b)); }
    @SuppressWarnings("unchecked")
    static <T extends Comparable<? super T>> Comparator<T> naturalOrder() { return (a, b) -> a.compareTo(b); }
    @SuppressWarnings("unchecked")
    static <T extends Comparable<? super T>> Comparator<T> reverseOrder() { return (a, b) -> b.compareTo(a); }
    static <T> Comparator<T> nullsFirst(Comparator<? super T> c) { return (a, b) -> a == null ? (b == null ? 0 : -1) : (b == null ? 1 : c.compare(a, b)); }
    static <T> Comparator<T> nullsLast(Comparator<? super T> c) { return (a, b) -> a == null ? (b == null ? 0 : 1) : (b == null ? -1 : c.compare(a, b)); }
}
