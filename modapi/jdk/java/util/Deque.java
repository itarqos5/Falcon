package java.util;

public interface Deque<E> extends Collection<E> {
    void addFirst(E e);
    void addLast(E e);
    E removeFirst();
    E removeLast();
    E peekFirst();
    E peekLast();
    E pop();
    void push(E e);
    E peek();
    E poll();
    E pollFirst();
    E pollLast();
    boolean offer(E e);
    boolean offerFirst(E e);
    boolean offerLast(E e);
    E getFirst();
    E getLast();
}
