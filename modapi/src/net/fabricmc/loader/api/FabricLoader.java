package net.fabricmc.loader.api;

import java.nio.file.Path;
import java.util.Collection;
import java.util.Optional;
import net.fabricmc.api.EnvType;

public interface FabricLoader {
    static FabricLoader getInstance() { return net.falcon.FalconLoader.INSTANCE; }
    Path getGameDir();
    Path getConfigDir();
    Path getModsDir();
    boolean isDevelopmentEnvironment();
    EnvType getEnvironmentType();
    boolean isModLoaded(String id);
    Optional<ModContainer> getModContainer(String id);
    Collection<ModContainer> getAllMods();
}
