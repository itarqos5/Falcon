package net.falcon.jdk;

import java.util.ArrayList;
import java.util.List;
import java.util.stream.Stream;

public final class ArraysExt {
    private ArraysExt() {}

    public static <T> Stream<T> stream(T[] a) { List<T> l = new ArrayList<>(); for (T t : a) l.add(t); return new ListStream<>(l); }
    public static boolean equals(int[] a, int[] b) { if (a == b) return true; if (a == null || b == null || a.length != b.length) return false; for (int i = 0; i < a.length; i++) { if (a[i] != b[i]) return false; } return true; }
    @SuppressWarnings("unchecked")
    public static <T> T[] copyOf(T[] a, int length) { T[] out = (T[]) java.lang.reflect.Array.newInstance(a.getClass().getComponentType(), length); for (int i = 0; i < length && i < a.length; i++) out[i] = a[i]; return out; }
}
