package net.minecraft.client.multiplayer;

import java.util.ArrayList;
import java.util.List;
import net.minecraft.client.Minecraft;
import net.minecraft.client.player.AbstractClientPlayer;
import net.minecraft.core.BlockPos;
import net.minecraft.core.Direction;
import net.minecraft.world.level.ClipContext;
import net.minecraft.world.level.Level;
import net.minecraft.world.level.biome.BiomeManager;
import net.minecraft.world.phys.BlockHitResult;
import net.minecraft.world.phys.Vec3;

public class ClientLevel extends Level {
    private final Minecraft minecraft;
    private final BiomeManager biomeManager = new BiomeManager();

    public ClientLevel(Minecraft minecraft) { this.minecraft = minecraft; }

    public void refresh(String dimension) { this.dimension = dimension; }
    public BiomeManager getBiomeManager() { return biomeManager; }
    public List<AbstractClientPlayer> players() { List<AbstractClientPlayer> l = new ArrayList<>(); if (minecraft.player != null) l.add(minecraft.player); return l; }

    // The first solid block along a line (stepping through it in small pieces).
    public BlockHitResult clip(ClipContext context) {
        Vec3 from = context.from(), to = context.to();
        double length = from.distanceTo(to);
        int steps = (int) Math.ceil(length * 8.0);
        Vec3 delta = to.subtract(from);
        BlockPos last = BlockPos.containing(from);
        for (int i = 1; i <= steps; i++) {
            Vec3 p = from.add(delta.scale((double) i / steps));
            BlockPos pos = BlockPos.containing(p);
            if (pos.equals(last)) continue;
            if (getBlockState(pos).canOcclude()) {
                Direction face = Direction.UP;
                int dx = last.getX() - pos.getX(), dy = last.getY() - pos.getY(), dz = last.getZ() - pos.getZ();
                for (Direction d : Direction.values()) {
                    if (d.getStepX() == dx && d.getStepY() == dy && d.getStepZ() == dz) face = d;
                }
                return new BlockHitResult(p, face, pos, false);
            }
            last = pos;
        }
        return BlockHitResult.miss(to, Direction.UP, BlockPos.containing(to));
    }

    public BlockHitResult pickFromCamera(double reach) {
        net.minecraft.client.Camera cam = minecraft.gameRenderer.getMainCamera();
        Vec3 from = cam.position();
        Vec3 look = Vec3.directionFromRotation(cam.xRot(), cam.yRot());
        return clip(new ClipContext(from, from.add(look.scale(reach))));
    }
}
