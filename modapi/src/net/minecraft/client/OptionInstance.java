package net.minecraft.client;

public class OptionInstance<T> {
    private T value;

    public OptionInstance(T value) { this.value = value; }
    public T get() { return value; }
    public void set(T v) { value = v; }
}
