package net.minecraft.world.phys.shapes;

import java.util.ArrayList;
import java.util.List;
import net.minecraft.world.phys.AABB;

public class VoxelShape {
    private final List<AABB> boxes;

    public VoxelShape(List<AABB> boxes) { this.boxes = boxes; }

    public boolean isEmpty() { return boxes.isEmpty(); }
    public List<AABB> toAabbs() { return boxes; }
    public AABB bounds() {
        if (boxes.isEmpty()) throw new UnsupportedOperationException("No bounds for empty shape.");
        AABB b = boxes.get(0);
        for (int i = 1; i < boxes.size(); i++) b = b.minmax(boxes.get(i));
        return b;
    }
    public double min(net.minecraft.core.Direction.Axis a) { AABB b = bounds(); return a == net.minecraft.core.Direction.Axis.X ? b.minX : (a == net.minecraft.core.Direction.Axis.Y ? b.minY : b.minZ); }
    public double max(net.minecraft.core.Direction.Axis a) { AABB b = bounds(); return a == net.minecraft.core.Direction.Axis.X ? b.maxX : (a == net.minecraft.core.Direction.Axis.Y ? b.maxY : b.maxZ); }
    public VoxelShape move(double x, double y, double z) { List<AABB> out = new ArrayList<>(); for (AABB b : boxes) out.add(b.move(x, y, z)); return new VoxelShape(out); }

    // Every edge of every box, as a line.
    public void forAllEdges(Shapes.DoubleLineConsumer consumer) {
        for (AABB b : boxes) {
            double[] xs = {b.minX, b.maxX}, ys = {b.minY, b.maxY}, zs = {b.minZ, b.maxZ};
            for (int j = 0; j < 2; j++) {
                for (int k = 0; k < 2; k++) {
                    consumer.consume(xs[0], ys[j], zs[k], xs[1], ys[j], zs[k]);
                    consumer.consume(xs[j], ys[0], zs[k], xs[j], ys[1], zs[k]);
                    consumer.consume(xs[j], ys[k], zs[0], xs[j], ys[k], zs[1]);
                }
            }
        }
    }
}
