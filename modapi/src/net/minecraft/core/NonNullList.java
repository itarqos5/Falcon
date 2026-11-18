package net.minecraft.core;

import java.util.ArrayList;

public class NonNullList<E> extends ArrayList<E> {
    public static <E> NonNullList<E> create() { return new NonNullList<>(); }
    public static <E> NonNullList<E> withSize(int size, E fill) { NonNullList<E> l = new NonNullList<>(); for (int i = 0; i < size; i++) l.add(fill); return l; }
}
