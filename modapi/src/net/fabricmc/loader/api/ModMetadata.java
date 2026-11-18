package net.fabricmc.loader.api;

import java.util.Collection;
import java.util.Optional;

public interface ModMetadata {
    String getId();
    String getName();
    Version getVersion();
    String getDescription();
    Collection<String> getAuthorNames();
    Optional<String> getRootPath();
}
