package net.minecraft.world.phys.shapes;

public interface CollisionContext {
    static CollisionContext empty() { return new CollisionContext() {}; }
    static CollisionContext of(net.minecraft.world.entity.Entity entity) { return new CollisionContext() {}; }
}
