package net.minecraft.world.level.material;

public class FluidState {
    private final Fluid fluid;

    public FluidState(Fluid fluid) { this.fluid = fluid; }
    public boolean isEmpty() { return fluid == null; }
    public boolean is(Object f) { return fluid != null && fluid == f; }
}
