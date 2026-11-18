package java.util.function;

@FunctionalInterface
public interface Predicate<T> {
    boolean test(T t);

    default Predicate<T> and(Predicate<? super T> other) { return t -> test(t) && other.test(t); }
    default Predicate<T> or(Predicate<? super T> other) { return t -> test(t) || other.test(t); }
    default Predicate<T> negate() { return t -> !test(t); }
    static <T> Predicate<T> not(Predicate<? super T> target) { return t -> !target.test(t); }
    static <T> Predicate<T> isEqual(Object o) { return t -> java.util.Objects.equals(o, t); }
}
