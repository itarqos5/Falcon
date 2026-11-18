package net.minecraft.client;

import com.mojang.blaze3d.platform.InputConstants;
import java.util.ArrayList;
import java.util.List;
import net.falcon.Host;
import net.minecraft.network.chat.Component;
import net.minecraft.resources.Identifier;

public class KeyMapping implements Comparable<KeyMapping> {
    private static final List<KeyMapping> MODS = new ArrayList<>();
    private final String name;
    private final InputConstants.Key defaultKey;
    private InputConstants.Key key;
    private final Category category;
    private boolean down;

    public KeyMapping(String name, InputConstants.Type type, int code, Category category) {
        this.name = name;
        this.defaultKey = type.getOrCreate(code);
        this.key = defaultKey;
        this.category = category;
    }

    public KeyMapping(String name, int code, Category category) { this(name, InputConstants.Type.KEYBOARD, code, category); }
    // (a category given as the translation key of its heading, as the game's older versions had it)
    public KeyMapping(String name, InputConstants.Type type, int code, String category) { this(name, type, code, new Category(Identifier.fromNamespaceAndPath("raw", category))); }
    public KeyMapping(String name, int code, String category) { this(name, InputConstants.Type.KEYBOARD, code, category); }

    // A key a mod asks for: the client lists it on the Key Binds screen and answers isDown and consumeClick for it.
    public static void registerMod(KeyMapping mapping) {
        if (!MODS.contains(mapping)) {
            MODS.add(mapping);
            Host.registerKey(mapping.name, mapping.category.translationKey(), mapping.defaultKey.getType() == InputConstants.Type.MOUSE ? 1 : 0, mapping.defaultKey.getValue());
            net.minecraft.client.Minecraft.getInstance().options.addKeyMapping(mapping);
        }
    }

    public String getName() { return name; }
    public Category getCategory() { return category; }
    public InputConstants.Key getDefaultKey() { return defaultKey; }
    public boolean isDefault() { return key.equals(defaultKey); }
    public boolean isUnbound() { return key.equals(InputConstants.UNKNOWN); }
    public void setKey(InputConstants.Key k) { key = k; }
    public void resetMapping() { key = defaultKey; }
    public String saveString() { return key.getName(); }
    public void setDown(boolean d) { down = d; }
    public boolean isDown() { return down || Host.isDown(name); }
    public boolean consumeClick() { return Host.consumeClick(name); }
    public Component getTranslatedKeyMessage() { return key.getDisplayName(); }
    public boolean matches(net.minecraft.client.input.KeyEvent event) { return !isUnbound() && key.getType() == InputConstants.Type.KEYBOARD && key.getValue() == event.key(); }
    @Override public int compareTo(KeyMapping o) { int c = category.id().compareTo(o.category.id()); return c != 0 ? c : name.compareTo(o.name); }

    public record Category(Identifier id) {
        public static final Category MOVEMENT = new Category(Identifier.withDefaultNamespace("movement"));
        public static final Category MISC = new Category(Identifier.withDefaultNamespace("misc"));
        public static final Category MULTIPLAYER = new Category(Identifier.withDefaultNamespace("multiplayer"));
        public static final Category GAMEPLAY = new Category(Identifier.withDefaultNamespace("gameplay"));
        public static final Category INVENTORY = new Category(Identifier.withDefaultNamespace("inventory"));
        public static final Category CREATIVE = new Category(Identifier.withDefaultNamespace("creative"));
        public static final Category SPECTATOR = new Category(Identifier.withDefaultNamespace("spectator"));
        public static final Category DEBUG = new Category(Identifier.withDefaultNamespace("debug"));

        public static Category register(Identifier id) { return new Category(id); }
        public String translationKey() { return id.getNamespace().equals("raw") ? id.getPath() : "key.category." + id.getNamespace() + "." + id.getPath(); }
        public Component label() { return Component.translatable(translationKey()); }
    }
}
