package net.minecraft.core;

import net.minecraft.resources.Identifier;

public interface ClientAsset {
    Identifier texturePath();

    record Texture(Identifier texturePath) implements ClientAsset {}
    record DownloadedTexture(Identifier texturePath, String url) implements ClientAsset {}
}
