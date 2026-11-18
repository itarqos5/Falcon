package net.falcon.jdk;

import java.util.ArrayList;
import java.util.Collection;
import java.util.Iterator;
import java.util.List;
import java.util.function.Predicate;
import java.util.stream.Stream;

// What a Collection does in Java but the VM's native collections do not do themselves: the VM looks for a static method here, with the collection first, when a native class has no such method.
public final class CollectionExt {
    private CollectionExt() {}

    public static <T> Stream<T> stream(Collection<T> c) { return new ListStream<>(new ArrayList<>(c)); }
    public static <T> Stream<T> parallelStream(Collection<T> c) { return stream(c); }

    public static <T> boolean removeIf(Collection<T> c, Predicate<? super T> p) {
        List<T> keep = new ArrayList<>();
        boolean removed = false;
        for (Iterator<T> it = c.iterator(); it.hasNext(); ) {
            T t = it.next();
            if (p.test(t)) removed = true; else keep.add(t);
        }
        if (removed) { c.clear(); c.addAll(keep); }
        return removed;
    }

    public static <T> boolean retainAll(Collection<T> c, Collection<?> other) { return removeIf(c, t -> !other.contains(t)); }
    public static <T> boolean removeAll(Collection<T> c, Collection<?> other) { return removeIf(c, t -> other.contains(t)); }
    public static <T> boolean containsAll(Collection<T> c, Collection<?> other) { for (Object o : other) { if (!c.contains(o)) return false; } return true; }
    public static <T> Object[] toArray(Collection<T> c) { Object[] a = new Object[c.size()]; int i = 0; for (T t : c) a[i++] = t; return a; }
}
