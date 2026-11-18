package net.falcon.jdk;

import java.util.ArrayList;
import java.util.Collection;
import java.util.Comparator;
import java.util.List;

public final class ListExt {
    private ListExt() {}

    @SuppressWarnings("unchecked")
    public static <T> void sort(List<T> list, Comparator<? super T> c) {
        int n = list.size();
        Object[] a = new Object[n];
        for (int i = 0; i < n; i++) a[i] = list.get(i);
        Object[] tmp = new Object[n];
        mergeSort(a, tmp, 0, n, (Comparator<Object>) c);
        for (int i = 0; i < n; i++) list.set(i, (T) a[i]);
    }

    @SuppressWarnings("unchecked")
    private static void mergeSort(Object[] a, Object[] tmp, int lo, int hi, Comparator<Object> c) {
        if (hi - lo < 2) return;
        int mid = (lo + hi) >>> 1;
        mergeSort(a, tmp, lo, mid, c);
        mergeSort(a, tmp, mid, hi, c);
        int i = lo, j = mid, k = lo;
        while (i < mid && j < hi) {
            int r = c == null ? ((Comparable<Object>) a[i]).compareTo(a[j]) : c.compare(a[i], a[j]);
            tmp[k++] = r <= 0 ? a[i++] : a[j++];
        }
        while (i < mid) tmp[k++] = a[i++];
        while (j < hi) tmp[k++] = a[j++];
        for (int x = lo; x < hi; x++) a[x] = tmp[x];
    }

    public static <T> void forEach(List<T> list, java.util.function.Consumer<? super T> action) { for (int i = 0; i < list.size(); i++) action.accept(list.get(i)); }
    public static <T> List<T> subList(List<T> list, int from, int to) { List<T> out = new ArrayList<>(); for (int i = from; i < to; i++) out.add(list.get(i)); return out; }
    public static <T> List<T> reversed(List<T> list) { List<T> out = new ArrayList<>(); for (int i = list.size() - 1; i >= 0; i--) out.add(list.get(i)); return out; }
    public static <T> void replaceAll(List<T> list, java.util.function.UnaryOperator<T> op) { for (int i = 0; i < list.size(); i++) list.set(i, op.apply(list.get(i))); }
    public static <T> List<T> copyOf(Collection<? extends T> c) { return new ArrayList<>(c); }
    public static <T> List<T> of() { return new ArrayList<>(); }
    public static <T> List<T> of(T a) { List<T> l = new ArrayList<>(); l.add(a); return l; }
    public static <T> List<T> of(T a, T b) { List<T> l = of(a); l.add(b); return l; }
    public static <T> List<T> of(T a, T b, T c) { List<T> l = of(a, b); l.add(c); return l; }
    public static <T> List<T> of(T a, T b, T c, T d) { List<T> l = of(a, b, c); l.add(d); return l; }
    public static <T> List<T> of(T a, T b, T c, T d, T e) { List<T> l = of(a, b, c, d); l.add(e); return l; }
    public static <T> List<T> of(T a, T b, T c, T d, T e, T f) { List<T> l = of(a, b, c, d, e); l.add(f); return l; }
    public static <T> List<T> of(T[] values) { List<T> l = new ArrayList<>(); for (T v : values) l.add(v); return l; }
}
