package com.example.test.mixin;

import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.injection.At;
import org.spongepowered.asm.mixin.injection.Inject;

@Mixin(targets = "net.minecraft.client.gui.screens.TitleScreen")
public class TitleMixin {
    @Inject(method = "init", at = @At("HEAD"))
    private void test$init() {}
}
