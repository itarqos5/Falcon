package net.minecraft.client.resources;

import net.minecraft.world.entity.player.PlayerSkin;

public final class DefaultPlayerSkin {
    private DefaultPlayerSkin() {}

    public static PlayerSkin get(java.util.UUID id) { return new PlayerSkin(); }
}
