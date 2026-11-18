package net.minecraft.world.inventory;

import net.minecraft.world.entity.player.Inventory;

public class MenuType<T extends AbstractContainerMenu> {
    public T create(int containerId, Inventory inventory) {
        return null;
    }
}
