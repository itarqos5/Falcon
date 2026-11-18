package net.falcon;

import java.nio.file.Path;
import java.nio.file.Paths;
import java.util.ArrayList;
import java.util.Collection;
import java.util.List;
import java.util.Optional;
import net.fabricmc.api.EnvType;
import net.fabricmc.loader.api.FabricLoader;
import net.fabricmc.loader.api.ModContainer;

// The mod loader the mods see: Falcon's own, answering what Fabric's loader answers.
public final class FalconLoader implements FabricLoader {
    public static final FalconLoader INSTANCE = new FalconLoader();
    private List<ModContainer> mods;

    private List<ModContainer> mods() {
        if (mods == null) {
            mods = new ArrayList<>();
            for (String entry : Host.loadedMods()) {
                String[] parts = entry.split("\u0000", -1);
                mods.add(new FalconModContainer(parts[0], parts.length > 1 ? parts[1] : "", parts.length > 2 ? parts[2] : parts[0]));
            }
            mods.add(new FalconModContainer("minecraft", "26.3", "Minecraft"));
            mods.add(new FalconModContainer("fabricloader", "0.19.2", "Fabric Loader"));
            mods.add(new FalconModContainer("fabric-api", "0.150.0", "Fabric API"));
            mods.add(new FalconModContainer("java", "25", "Java"));
        }
        return mods;
    }

    @Override public Path getGameDir() { return Paths.get(Host.gameDir()); }
    @Override public Path getConfigDir() { return Paths.get(Host.gameDir(), "config"); }
    @Override public Path getModsDir() { return Paths.get(Host.modsDir()); }
    @Override public boolean isDevelopmentEnvironment() { return false; }
    @Override public EnvType getEnvironmentType() { return EnvType.CLIENT; }

    @Override
    public boolean isModLoaded(String id) {
        for (ModContainer m : mods()) {
            if (m.getMetadata().getId().equals(id)) return true;
        }
        return false;
    }

    @Override
    public Optional<ModContainer> getModContainer(String id) {
        for (ModContainer m : mods()) {
            if (m.getMetadata().getId().equals(id)) return Optional.of(m);
        }
        return Optional.empty();
    }

    @Override public Collection<ModContainer> getAllMods() { return mods(); }
}
