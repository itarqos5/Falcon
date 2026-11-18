package net.minecraft.network.codec;

public interface StreamCodec<B, V> {
    static <B, V> StreamCodec<B, V> of(Object ignored) { return null; }
}
