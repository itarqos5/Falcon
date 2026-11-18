package java.util.stream;

import java.util.OptionalInt;
import java.util.function.IntConsumer;
import java.util.function.IntFunction;
import java.util.function.IntPredicate;
import java.util.function.IntUnaryOperator;

public interface IntStream {
    IntStream filter(IntPredicate p);
    IntStream map(IntUnaryOperator f);
    <U> Stream<U> mapToObj(IntFunction<? extends U> f);
    Stream<Integer> boxed();
    int sum();
    OptionalInt max();
    OptionalInt min();
    long count();
    void forEach(IntConsumer c);
    int[] toArray();
    boolean anyMatch(IntPredicate p);
    boolean allMatch(IntPredicate p);
    boolean noneMatch(IntPredicate p);

    static IntStream range(int from, int to) { int[] a = new int[Math.max(0, to - from)]; for (int i = 0; i < a.length; i++) a[i] = from + i; return new net.falcon.jdk.IntArrayStream(a); }
    static IntStream rangeClosed(int from, int to) { return range(from, to + 1); }
    static IntStream of(int... values) { return new net.falcon.jdk.IntArrayStream(values); }
}
