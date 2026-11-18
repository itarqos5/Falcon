package net.minecraft.network.protocol.common.custom;

import net.minecraft.network.codec.StreamCodec;
import net.minecraft.resources.Identifier;

public interface CustomPacketPayload {
    Type<? extends CustomPacketPayload> type();

    record Type<T extends CustomPacketPayload>(Identifier id) {
        @Override
        public String toString() { return id != null ? id.toString() : ""; }
    }

    record TypeAndCodec<B, T extends CustomPacketPayload>(Type<T> type, StreamCodec<B, T> codec) {}
}
