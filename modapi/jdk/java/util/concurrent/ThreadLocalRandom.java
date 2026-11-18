package java.util.concurrent;

public class ThreadLocalRandom extends java.util.Random {
    private static final ThreadLocalRandom INSTANCE = new ThreadLocalRandom();
    public static ThreadLocalRandom current() { return INSTANCE; }
}
