package net.minecraft.client.gui.screens.inventory;

import net.minecraft.client.gui.screens.Screen;
import net.minecraft.network.chat.Component;
import net.minecraft.world.inventory.AbstractContainerMenu;

public abstract class AbstractContainerScreen<T extends AbstractContainerMenu> extends Screen {
    protected final T menu;

    public AbstractContainerScreen(T menu, Component title) {
        super(title);
        this.menu = menu;
    }

    public T getMenu() {
        return menu;
    }
}
