package net.fabricmc.fabric.api.client.rendering.v1.level;

import net.fabricmc.fabric.api.event.Event;
import net.fabricmc.fabric.api.event.EventFactory;

public final class LevelRenderEvents {
    private LevelRenderEvents() {}

    public static final Event<CollectSubmits> COLLECT_SUBMITS = EventFactory.createArrayBacked(CollectSubmits.class, callbacks -> context -> { for (CollectSubmits c : callbacks) c.collectSubmits(context); });
    public static final Event<BeforeBlockOutline> BEFORE_BLOCK_OUTLINE = EventFactory.createArrayBacked(BeforeBlockOutline.class, callbacks -> (context, hit) -> { for (BeforeBlockOutline c : callbacks) { if (!c.beforeBlockOutline(context, hit)) return false; } return true; });
    public static final Event<AfterEntities> AFTER_ENTITIES = EventFactory.createArrayBacked(AfterEntities.class, callbacks -> context -> { for (AfterEntities c : callbacks) c.afterEntities(context); });
    public static final Event<AfterTranslucent> AFTER_TRANSLUCENT = EventFactory.createArrayBacked(AfterTranslucent.class, callbacks -> context -> { for (AfterTranslucent c : callbacks) c.afterTranslucent(context); });
    public static final Event<EndMain> END_MAIN = EventFactory.createArrayBacked(EndMain.class, callbacks -> context -> { for (EndMain c : callbacks) c.endMain(context); });

    public interface CollectSubmits { void collectSubmits(LevelRenderContext context); }
    public interface BeforeBlockOutline { boolean beforeBlockOutline(LevelRenderContext context, net.minecraft.world.phys.HitResult hit); }
    public interface AfterEntities { void afterEntities(LevelRenderContext context); }
    public interface AfterTranslucent { void afterTranslucent(LevelRenderContext context); }
    public interface EndMain { void endMain(LevelRenderContext context); }
}
