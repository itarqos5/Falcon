package net.fabricmc.fabric.api.event.player;

import net.fabricmc.fabric.api.event.Event;
import net.fabricmc.fabric.api.event.EventFactory;
import net.minecraft.world.InteractionHand;
import net.minecraft.world.InteractionResult;
import net.minecraft.world.entity.player.Player;
import net.minecraft.world.level.Level;
import net.minecraft.world.phys.BlockHitResult;

public interface UseBlockCallback {
    Event<UseBlockCallback> EVENT = EventFactory.createArrayBacked(UseBlockCallback.class, listeners -> (player, world, hand, hitResult) -> {
        for (UseBlockCallback l : listeners) {
            InteractionResult r = l.interact(player, world, hand, hitResult);
            if (r != InteractionResult.PASS) return r;
        }
        return InteractionResult.PASS;
    });

    InteractionResult interact(Player player, Level world, InteractionHand hand, BlockHitResult hitResult);
}
