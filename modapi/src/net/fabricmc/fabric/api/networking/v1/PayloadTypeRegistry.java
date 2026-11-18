package net.fabricmc.fabric.api.networking.v1;

public class PayloadTypeRegistry {
    private static final PayloadTypeRegistry INSTANCE = new PayloadTypeRegistry();
    public static PayloadTypeRegistry clientboundPlay() { return INSTANCE; }
    public static PayloadTypeRegistry serverboundPlay() { return INSTANCE; }
    public static PayloadTypeRegistry playC2S() { return INSTANCE; }
    public static PayloadTypeRegistry playS2C() { return INSTANCE; }
    public Object register(Object type, Object codec) { return type; }
}
