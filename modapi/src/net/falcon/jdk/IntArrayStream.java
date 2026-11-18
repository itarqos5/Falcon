package net.falcon.jdk;

import java.util.ArrayList;
import java.util.List;
import java.util.OptionalInt;
import java.util.function.IntConsumer;
import java.util.function.IntFunction;
import java.util.function.IntPredicate;
import java.util.function.IntUnaryOperator;
import java.util.stream.IntStream;
import java.util.stream.Stream;

public final class IntArrayStream implements IntStream {
    private final int[] values;

    public IntArrayStream(int[] values) { this.values = values; }

    @Override public IntStream filter(IntPredicate p) { int n = 0; int[] t = new int[values.length]; for (int v : values) { if (p.test(v)) t[n++] = v; } int[] out = new int[n]; for (int i = 0; i < n; i++) out[i] = t[i]; return new IntArrayStream(out); }
    @Override public IntStream map(IntUnaryOperator f) { int[] out = new int[values.length]; for (int i = 0; i < out.length; i++) out[i] = f.applyAsInt(values[i]); return new IntArrayStream(out); }
    @Override public <U> Stream<U> mapToObj(IntFunction<? extends U> f) { List<U> out = new ArrayList<>(); for (int v : values) out.add(f.apply(v)); return new ListStream<>(out); }
    @Override public Stream<Integer> boxed() { List<Integer> out = new ArrayList<>(); for (int v : values) out.add(v); return new ListStream<>(out); }
    @Override public int sum() { int s = 0; for (int v : values) s += v; return s; }
    @Override public OptionalInt max() { if (values.length == 0) return OptionalInt.empty(); int m = values[0]; for (int v : values) { if (v > m) m = v; } return OptionalInt.of(m); }
    @Override public OptionalInt min() { if (values.length == 0) return OptionalInt.empty(); int m = values[0]; for (int v : values) { if (v < m) m = v; } return OptionalInt.of(m); }
    @Override public long count() { return values.length; }
    @Override public void forEach(IntConsumer c) { for (int v : values) c.accept(v); }
    @Override public int[] toArray() { int[] out = new int[values.length]; for (int i = 0; i < out.length; i++) out[i] = values[i]; return out; }
    @Override public boolean anyMatch(IntPredicate p) { for (int v : values) { if (p.test(v)) return true; } return false; }
    @Override public boolean allMatch(IntPredicate p) { for (int v : values) { if (!p.test(v)) return false; } return true; }
    @Override public boolean noneMatch(IntPredicate p) { return !anyMatch(p); }
}
