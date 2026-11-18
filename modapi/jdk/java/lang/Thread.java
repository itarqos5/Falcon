package java.lang;

// There is one thread in the VM: a thread a mod starts runs its code at once, to its end.
public class Thread implements Runnable {
    private static final Thread MAIN = new Thread(null, "main");
    private final Runnable target;
    private String name;
    private boolean daemon, interrupted;

    public Thread(Runnable target) { this(target, "Thread"); }
    public Thread(Runnable target, String name) { this.target = target; this.name = name; }

    public static Thread currentThread() { return MAIN; }
    public static void yield() {}
    public static void sleep(long millis) throws InterruptedException {}
    public static boolean interrupted() { return false; }
    public void start() { run(); }
    @Override public void run() { if (target != null) target.run(); }
    public void interrupt() { interrupted = true; }
    public boolean isInterrupted() { return interrupted; }
    public boolean isAlive() { return false; }
    public void join() throws InterruptedException {}
    public void setDaemon(boolean d) { daemon = d; }
    public boolean isDaemon() { return daemon; }
    public void setPriority(int p) {}
    public void setName(String n) { name = n; }
    public String getName() { return name; }
}
