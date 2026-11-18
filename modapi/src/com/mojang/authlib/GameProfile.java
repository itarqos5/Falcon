package com.mojang.authlib;

import java.util.UUID;
import com.mojang.authlib.properties.PropertyMap;

public record GameProfile(UUID id, String name, PropertyMap properties) {
    public GameProfile(UUID id, String name) { this(id, name, new PropertyMap()); }
}
