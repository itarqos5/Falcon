package net.minecraft.client;

public class DeltaTracker {
    private final float partial;

    public DeltaTracker(float partial) { this.partial = partial; }
    public float getGameTimeDeltaPartialTick(boolean ignoreFreeze) { return partial; }
    public float getGameTimeDeltaTicks() { return 1.0F; }
    public float getRealtimeDeltaTicks() { return 1.0F; }
}
