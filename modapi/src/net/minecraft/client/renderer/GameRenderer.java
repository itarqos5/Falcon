package net.minecraft.client.renderer;

import net.minecraft.client.Camera;

public class GameRenderer {
    private final Camera camera = new Camera();

    public Camera mainCamera() { return camera; }
    public Camera getMainCamera() { return camera; }
}
