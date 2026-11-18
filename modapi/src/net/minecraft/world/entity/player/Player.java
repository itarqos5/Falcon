package net.minecraft.world.entity.player;

import com.mojang.authlib.GameProfile;
import net.minecraft.network.chat.Component;
import net.minecraft.world.entity.LivingEntity;

public class Player extends LivingEntity {
    protected boolean creative, spectator;
    protected String name = "";
    protected java.util.UUID uuid = new java.util.UUID(0L, 0L);

    public boolean isCreative() { return creative; }
    public boolean isSpectator() { return spectator; }
    public Component getName() { return Component.literal(name); }
    @Override public java.util.UUID getUUID() { return uuid; }
    public GameProfile getGameProfile() { return new GameProfile(uuid, name); }
    public Inventory getInventory() { return new Inventory(); }
    public boolean canEat(boolean always) { return true; }
    public boolean canUseGameMasterBlocks() { return creative; }
    public java.util.Optional<net.minecraft.core.GlobalPos> getLastDeathLocation() { return java.util.Optional.empty(); }
}
