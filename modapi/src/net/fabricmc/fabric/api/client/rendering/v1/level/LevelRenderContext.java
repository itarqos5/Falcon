package net.fabricmc.fabric.api.client.rendering.v1.level;

import com.mojang.blaze3d.vertex.PoseStack;
import net.minecraft.client.Camera;
import net.minecraft.client.renderer.SubmitNodeCollector;

public interface LevelRenderContext {
    PoseStack poseStack();
    SubmitNodeCollector submitNodeCollector();
    Camera camera();
    float tickDelta();
}
