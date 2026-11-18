package net.minecraft.world;

public class InteractionResult {
    public static final InteractionResult PASS = new InteractionResult("pass");
    public static final InteractionResult FAIL = new InteractionResult("fail");
    public static final InteractionResult SUCCESS = new Success();
    public static final InteractionResult CONSUME = new InteractionResult("consume");
    public static final InteractionResult SUCCESS_SERVER = new Success();
    private final String name;

    protected InteractionResult(String name) { this.name = name; }
    public boolean consumesAction() { return this instanceof Success || this == CONSUME; }
    @Override public String toString() { return name; }

    public static final class Success extends InteractionResult {
        public Success() { super("success"); }
    }
}
