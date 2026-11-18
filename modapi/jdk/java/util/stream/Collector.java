package java.util.stream;

import java.util.function.BiConsumer;
import java.util.function.Function;
import java.util.function.Supplier;

public interface Collector<T, A, R> {
    Supplier<A> supplier();
    BiConsumer<A, T> accumulator();
    Function<A, R> finisher();

    static <T, A, R> Collector<T, A, R> of(Supplier<A> supplier, BiConsumer<A, T> accumulator, Function<A, R> finisher) {
        return new Collector<T, A, R>() {
            @Override public Supplier<A> supplier() { return supplier; }
            @Override public BiConsumer<A, T> accumulator() { return accumulator; }
            @Override public Function<A, R> finisher() { return finisher; }
        };
    }
}
