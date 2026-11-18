package java.util.stream;

import java.util.ArrayList;
import java.util.HashMap;
import java.util.HashSet;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Map;
import java.util.Set;
import java.util.function.BinaryOperator;
import java.util.function.Function;
import java.util.function.Predicate;
import java.util.function.Supplier;
import java.util.function.ToIntFunction;

public final class Collectors {
    private Collectors() {}

    public static <T> Collector<T, ?, List<T>> toList() { return Collector.<T, List<T>, List<T>>of(ArrayList::new, List::add, l -> l); }
    public static <T> Collector<T, ?, List<T>> toUnmodifiableList() { return toList(); }
    public static <T> Collector<T, ?, Set<T>> toSet() { return Collector.<T, Set<T>, Set<T>>of(HashSet::new, Set::add, s -> s); }
    public static <T> Collector<T, ?, Set<T>> toUnmodifiableSet() { return toSet(); }

    public static <T, K, U> Collector<T, ?, Map<K, U>> toMap(Function<? super T, ? extends K> key, Function<? super T, ? extends U> value) {
        return Collector.<T, Map<K, U>, Map<K, U>>of(HashMap::new, (m, t) -> {
            K k = key.apply(t);
            if (m.containsKey(k)) throw new IllegalStateException("Duplicate key " + k);
            m.put(k, value.apply(t));
        }, m -> m);
    }

    public static <T, K, U> Collector<T, ?, Map<K, U>> toMap(Function<? super T, ? extends K> key, Function<? super T, ? extends U> value, BinaryOperator<U> merge) {
        return Collector.<T, Map<K, U>, Map<K, U>>of(HashMap::new, (m, t) -> {
            K k = key.apply(t);
            U v = value.apply(t);
            m.put(k, m.containsKey(k) ? merge.apply(m.get(k), v) : v);
        }, m -> m);
    }

    public static <T, K, U, M extends Map<K, U>> Collector<T, ?, M> toMap(Function<? super T, ? extends K> key, Function<? super T, ? extends U> value, BinaryOperator<U> merge, Supplier<M> factory) {
        return Collector.<T, M, M>of(factory, (m, t) -> {
            K k = key.apply(t);
            U v = value.apply(t);
            m.put(k, m.containsKey(k) ? merge.apply(m.get(k), v) : v);
        }, m -> m);
    }

    public static Collector<CharSequence, ?, String> joining() { return joining("", "", ""); }
    public static Collector<CharSequence, ?, String> joining(CharSequence delimiter) { return joining(delimiter, "", ""); }
    public static Collector<CharSequence, ?, String> joining(CharSequence delimiter, CharSequence prefix, CharSequence suffix) {
        return Collector.<CharSequence, List<String>, String>of(ArrayList::new, (l, s) -> l.add(String.valueOf(s)), l -> {
            StringBuilder sb = new StringBuilder(String.valueOf(prefix));
            for (int i = 0; i < l.size(); i++) { if (i > 0) sb.append(delimiter); sb.append(l.get(i)); }
            return sb.append(suffix).toString();
        });
    }

    public static <T> Collector<T, ?, Long> counting() { return Collector.<T, long[], Long>of(() -> new long[1], (a, t) -> a[0]++, a -> a[0]); }
    public static <T> Collector<T, ?, Integer> summingInt(ToIntFunction<? super T> f) { return Collector.<T, int[], Integer>of(() -> new int[1], (a, t) -> a[0] += f.applyAsInt(t), a -> a[0]); }

    public static <T, K> Collector<T, ?, Map<K, List<T>>> groupingBy(Function<? super T, ? extends K> classifier) { return groupingBy(classifier, toList()); }

    @SuppressWarnings("unchecked")
    public static <T, K, A, D> Collector<T, ?, Map<K, D>> groupingBy(Function<? super T, ? extends K> classifier, Collector<? super T, A, D> downstream) {
        return Collector.<T, Map<K, A>, Map<K, D>>of(LinkedHashMap::new, (m, t) -> {
            K k = classifier.apply(t);
            A acc = m.get(k);
            if (acc == null) { acc = downstream.supplier().get(); m.put(k, acc); }
            ((java.util.function.BiConsumer<A, T>) downstream.accumulator()).accept(acc, t);
        }, m -> {
            Map<K, D> out = new LinkedHashMap<>();
            for (Map.Entry<K, A> e : m.entrySet()) out.put(e.getKey(), downstream.finisher().apply(e.getValue()));
            return out;
        });
    }

    @SuppressWarnings("unchecked")
    public static <T, U, A, R> Collector<T, ?, R> mapping(Function<? super T, ? extends U> mapper, Collector<? super U, A, R> downstream) {
        return Collector.<T, A, R>of(downstream.supplier(), (a, t) -> ((java.util.function.BiConsumer<A, U>) downstream.accumulator()).accept(a, mapper.apply(t)), downstream.finisher());
    }

    public static <T> Collector<T, ?, Map<Boolean, List<T>>> partitioningBy(Predicate<? super T> p) {
        return Collector.<T, Map<Boolean, List<T>>, Map<Boolean, List<T>>>of(() -> { Map<Boolean, List<T>> m = new HashMap<>(); m.put(Boolean.FALSE, new ArrayList<>()); m.put(Boolean.TRUE, new ArrayList<>()); return m; }, (m, t) -> m.get(p.test(t)).add(t), m -> m);
    }
}
