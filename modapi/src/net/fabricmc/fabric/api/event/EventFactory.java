package net.fabricmc.fabric.api.event;

import java.lang.reflect.Array;
import java.util.function.Function;

public final class EventFactory {
    private EventFactory() {}

    public static <T> Event<T> createArrayBacked(Class<? super T> type, Function<T[], T> invokerFactory) { return new ArrayBackedEvent<>(type, invokerFactory); }

    public static <T> Event<T> createArrayBacked(Class<T> type, T emptyInvoker, Function<T[], T> invokerFactory) { return new ArrayBackedEvent<>(type, invokerFactory); }

    public static <T> Event<T> createWithPhases(Class<? super T> type, Function<T[], T> invokerFactory, net.minecraft.resources.Identifier... defaultPhases) { return new ArrayBackedEvent<>(type, invokerFactory); }

    private static final class ArrayBackedEvent<T> extends Event<T> {
        private final Class<? super T> type;
        private final Function<T[], T> factory;
        private T[] handlers;

        @SuppressWarnings("unchecked")
        ArrayBackedEvent(Class<? super T> type, Function<T[], T> factory) {
            this.type = type;
            this.factory = factory;
            this.handlers = (T[]) Array.newInstance(type, 0);
            this.invoker = factory.apply(handlers);
        }

        @Override
        @SuppressWarnings("unchecked")
        public void register(T listener) {
            T[] next = (T[]) Array.newInstance(type, handlers.length + 1);
            for (int i = 0; i < handlers.length; i++) next[i] = handlers[i];
            next[handlers.length] = listener;
            handlers = next;
            invoker = factory.apply(handlers);
        }
    }
}
