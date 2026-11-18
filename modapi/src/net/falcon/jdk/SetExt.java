package net.falcon.jdk;

import java.util.Collection;
import java.util.HashSet;
import java.util.LinkedHashSet;
import java.util.Set;

public final class SetExt {
    private SetExt() {}

    public static <T> Set<T> copyOf(Collection<? extends T> c) { return new LinkedHashSet<>(c); }
    public static <T> Set<T> of() { return new LinkedHashSet<>(); }
    public static <T> Set<T> of(T a) { Set<T> s = of(); s.add(a); return s; }
    public static <T> Set<T> of(T a, T b) { Set<T> s = of(a); s.add(b); return s; }
    public static <T> Set<T> of(T a, T b, T c) { Set<T> s = of(a, b); s.add(c); return s; }
    public static <T> Set<T> of(T a, T b, T c, T d) { Set<T> s = of(a, b, c); s.add(d); return s; }
    public static <T> Set<T> of(T a, T b, T c, T d, T e) { Set<T> s = of(a, b, c, d); s.add(e); return s; }
    public static <T> Set<T> of(T[] values) { Set<T> s = of(); for (T v : values) s.add(v); return s; }
}
