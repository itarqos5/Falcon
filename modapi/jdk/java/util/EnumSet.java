package java.util;

public class EnumSet<E extends Enum<E>> extends LinkedHashSet<E> {
    public static <E extends Enum<E>> EnumSet<E> noneOf(Class<E> type) { return new EnumSet<>(); }
    @SafeVarargs public static <E extends Enum<E>> EnumSet<E> of(E first, E... rest) { EnumSet<E> s = new EnumSet<>(); s.add(first); for (E e : rest) s.add(e); return s; }
    public static <E extends Enum<E>> EnumSet<E> copyOf(Collection<E> c) { EnumSet<E> s = new EnumSet<>(); s.addAll(c); return s; }
}
