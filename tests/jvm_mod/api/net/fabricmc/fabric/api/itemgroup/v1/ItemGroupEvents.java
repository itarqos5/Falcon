package net.fabricmc.fabric.api.itemgroup.v1;
import net.fabricmc.fabric.api.event.Event;
public final class ItemGroupEvents {
    public interface ModifyEntries { void modifyEntries(FabricItemGroupEntries entries); }
    public static Event<ModifyEntries> modifyEntriesEvent(Object tab) { return null; }
}
