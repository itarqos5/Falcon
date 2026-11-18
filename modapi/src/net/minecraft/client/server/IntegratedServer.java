package net.minecraft.client.server;

import java.nio.file.Path;
import java.nio.file.Paths;
import net.minecraft.world.level.storage.LevelResource;

public class IntegratedServer {
    public net.minecraft.world.level.storage.WorldData getWorldData() { return new net.minecraft.world.level.storage.WorldData(); }
    public boolean isPublished() { return false; }
    public Path getWorldPath(LevelResource resource) { return Paths.get(net.falcon.Host.gameDir(), "saves", net.falcon.Host.worldName()); }
}
