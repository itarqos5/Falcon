package net.falcon.jdk;

import java.util.Collection;
import java.util.List;
import java.util.Map;

public final class CollectionsExt {
    private CollectionsExt() {}

    public static boolean disjoint(Collection<?> a, Collection<?> b) { for (Object o : a) { if (b.contains(o)) return false; } return true; }
    public static void reverse(List<?> list) { reverseOf(list); }
    @SuppressWarnings("unchecked")
    private static <T> void reverseOf(List<T> list) { for (int i = 0, j = list.size() - 1; i < j; i++, j--) { T t = list.get(i); list.set(i, list.get(j)); list.set(j, t); } }
    public static <K, V> Map<K, V> synchronizedMap(Map<K, V> m) { return m; }
    public static <T> List<T> synchronizedList(List<T> l) { return l; }
    public static <T> java.util.Set<T> synchronizedSet(java.util.Set<T> s) { return s; }
}
