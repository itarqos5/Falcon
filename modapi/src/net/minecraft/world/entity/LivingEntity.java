package net.minecraft.world.entity;

import net.minecraft.world.InteractionHand;
import net.minecraft.world.item.ItemStack;

public class LivingEntity extends Entity {
    protected float health = 20.0F;
    protected boolean alive = true;

    public float getHealth() { return health; }
    public boolean isAlive() { return alive; }
    public boolean isDeadOrDying() { return !alive || health <= 0.0F; }
    public ItemStack getItemInHand(InteractionHand hand) { return ItemStack.EMPTY; }
    public ItemStack getMainHandItem() { return ItemStack.EMPTY; }
    public ItemStack getOffhandItem() { return ItemStack.EMPTY; }
    public boolean swing(InteractionHand hand, net.minecraft.world.item.component.SwingAnimation animation, boolean sendToServer) { return false; }
}
