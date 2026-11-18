package java.lang;

import java.util.function.Supplier;

public class ThreadLocal<T> {
    private T value;
    private boolean set;
    private Supplier<? extends T> initial;

    public static <S> ThreadLocal<S> withInitial(Supplier<? extends S> supplier) { ThreadLocal<S> t = new ThreadLocal<>(); t.initial = supplier; return t; }
    protected T initialValue() { return initial == null ? null : initial.get(); }
    public T get() { if (!set) { value = initialValue(); set = true; } return value; }
    public void set(T v) { value = v; set = true; }
    public void remove() { set = false; value = null; }
}
