package net.fabricmc.fabric.api.networking.v1;

import net.minecraft.network.codec.StreamCodec;
import net.minecraft.network.protocol.common.custom.CustomPacketPayload;

public class PayloadTypeRegistry {
    private static final PayloadTypeRegistry INSTANCE = new PayloadTypeRegistry();
    public static PayloadTypeRegistry clientboundPlay() { return INSTANCE; }
    public static PayloadTypeRegistry serverboundPlay() { return INSTANCE; }
    public static PayloadTypeRegistry playC2S() { return INSTANCE; }
    public static PayloadTypeRegistry playS2C() { return INSTANCE; }

    public <T extends CustomPacketPayload> CustomPacketPayload.TypeAndCodec<?, T> register(CustomPacketPayload.Type<T> type, StreamCodec<?, T> codec) {
        return new CustomPacketPayload.TypeAndCodec<>(type, codec);
    }
}
