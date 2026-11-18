package net.falcon;

import java.util.Collection;
import java.util.List;
import java.util.Optional;
import net.fabricmc.loader.api.ModContainer;
import net.fabricmc.loader.api.ModMetadata;
import net.fabricmc.loader.api.Version;

final class FalconModContainer implements ModContainer, ModMetadata, Version {
    private final String id, version, name;

    FalconModContainer(String id, String version, String name) {
        this.id = id;
        this.version = version;
        this.name = name;
    }

    @Override public ModMetadata getMetadata() { return this; }
    @Override public String getId() { return id; }
    @Override public String getName() { return name; }
    @Override public Version getVersion() { return this; }
    @Override public String getFriendlyString() { return version; }
    @Override public String getDescription() { return ""; }
    @Override public Collection<String> getAuthorNames() { return List.of(); }
    @Override public Optional<String> getRootPath() { return Optional.empty(); }
    @Override public String toString() { return version; }
}
