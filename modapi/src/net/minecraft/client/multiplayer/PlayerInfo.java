package net.minecraft.client.multiplayer;

import com.mojang.authlib.GameProfile;
import net.minecraft.network.chat.Component;
import net.minecraft.world.entity.player.PlayerSkin;

public class PlayerInfo {
    private final GameProfile profile;

    public PlayerInfo(GameProfile profile, boolean showHat) { this.profile = profile; }

    public GameProfile getProfile() { return profile; }
    public PlayerSkin getSkin() { return new PlayerSkin(); }
    public Component getTabListDisplayName() { return null; }
    public net.minecraft.world.scores.PlayerTeam getTeam() { return null; }
}
