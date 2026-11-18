package java.util;

public class ArrayDeque<E> extends ArrayList<E> implements Deque<E>, Queue<E> {
    public ArrayDeque() { super(); }
    public ArrayDeque(int capacity) { super(); }
    public ArrayDeque(Collection<? extends E> c) { super(); addAll(c); }

    @Override public void addFirst(E e) { add(0, e); }
    @Override public void addLast(E e) { add(e); }
    @Override public E removeFirst() { if (isEmpty()) throw new NoSuchElementException(); return remove(0); }
    @Override public E removeLast() { if (isEmpty()) throw new NoSuchElementException(); return remove(size() - 1); }
    @Override public E peekFirst() { return isEmpty() ? null : get(0); }
    @Override public E peekLast() { return isEmpty() ? null : get(size() - 1); }
    @Override public E pop() { return removeFirst(); }
    @Override public void push(E e) { addFirst(e); }
    @Override public E peek() { return peekFirst(); }
    @Override public E poll() { return isEmpty() ? null : removeFirst(); }
    @Override public E pollFirst() { return poll(); }
    @Override public E pollLast() { return isEmpty() ? null : removeLast(); }
    @Override public boolean offer(E e) { addLast(e); return true; }
    @Override public boolean offerFirst(E e) { addFirst(e); return true; }
    @Override public boolean offerLast(E e) { addLast(e); return true; }
    @Override public E getFirst() { if (isEmpty()) throw new NoSuchElementException(); return get(0); }
    @Override public E getLast() { if (isEmpty()) throw new NoSuchElementException(); return get(size() - 1); }
    @Override public E remove() { return removeFirst(); }
    @Override public E element() { return getFirst(); }
}
