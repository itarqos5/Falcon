package net.falcon;

// What the client lends the mods. Every method here is answered in C++ (src/jvm/host_natives.cpp) from what the client set before the call; nothing in this class is the game's.
public final class Host {
    private Host() {}

    public static native String gameDir();
    public static native String modsDir();
    // One entry per loaded mod: "id\u0000version\u0000name".
    public static native String[] loadedMods();
    public static native void log(int level, String text);
    // The player's language: the text of a translation key, or the key itself.
    public static native String translate(String key);
    public static native int textWidth(String text);
    // x, y, z, yaw, pitch, eyeHeight, health, food, flags (1 sneaking, 2 on ground, 4 in water, 8 creative, 16 spectator, 32 alive)
    public static native double[] player();
    // gui width, gui height, framebuffer width, framebuffer height, gui scale, mouse x, mouse y (gui units), game time (ticks)
    public static native double[] window();
    // The camera: x, y, z, yaw, pitch, fov (degrees)
    public static native double[] camera();
    // "minecraft:overworld" and friends; empty when no world is open.
    public static native String dimension();
    // The name of what is being played: the world's folder, or the server's address; and whether it is a server (0 single player, 1 multiplayer, 2 LAN).
    public static native String worldName();
    public static native int worldKind();
    public static native String playerName();
    public static native String playerUuid();
    // The block at a place, as "minecraft:stone" ("minecraft:air" when it is not loaded); the game's flags: 1 loaded, 2 opaque, 4 full collision.
    public static native String blockAt(int x, int y, int z);
    public static native int blockFlags(int x, int y, int z);
    public static native boolean keyDown(int glfwKey);
    // Tells the player something (the chat), and returns at once.
    public static native void chat(String text);
    public static native void overlay(String text);
    // The mods' own key mappings: pressed since the last ask, held now, and the key a mapping is on ("key.keyboard.h").
    public static native boolean consumeClick(String mapping);
    public static native boolean isDown(String mapping);
    public static native void registerKey(String name, String category, int type, int code);
    public static native void registerItem(String id, int stacksTo, int durability, boolean fireResistant, String rarity, int nutrition, float saturation, String block);
    public static native void registerBlock(String id, float hardness, float resistance, int light, boolean requiresTool, boolean noOcclusion, String sound);
    public static native void creativeTab(String tab, String itemId);
    // Something was put into a registry ("item", "block" ...), for the Mods screen.
    public static native void registered(String registry, String id, String kind);
    // What the mod draws on the screen (the HUD, a screen): rectangles, text, pictures, items, scissors, in gui units.
    public static native void drawFill(float x0, float y0, float x1, float y1, int argb);
    public static native void drawGradient(float x0, float y0, float x1, float y1, int argb0, int argb1);
    public static native void drawText(String text, float x, float y, int argb, boolean shadow, boolean centered, float scale);
    public static native void drawTexture(String texture, float x0, float y0, float x1, float y1, float u, float v, float regionW, float regionH, int texW, int texH, int argb);
    public static native void drawSprite(String sprite, float x0, float y0, float x1, float y1, int argb);
    public static native void drawItem(String itemId, float x, float y);
    public static native void scissor(boolean on, float x0, float y0, float x1, float y1);
    // World geometry the mods submit: vertices (x y z r g b a u v: 9 floats each) with the topology (0 lines, 1 triangles, 2 quads), the state of the pipeline (bit 0 depth test, 1 depth write,
    // 2 blend, 3 cull, 4 line strip) and the line width; camera-relative, drawn after the entities.
    public static native void submit(float[] vertices, int vertexCount, int topology, int state, float lineWidth, String texture);
    // Pictures: the size of a PNG, and its pixels as ABGR ints (what the game keeps); sends a chat line or a command to the server.
    public static native int[] decodeImageSize(byte[] png);
    public static native void decodeImage(byte[] png, int[] pixels);
    public static native void sendChat(String text);
    public static native void sendCommand(String command);
    public static native void openScreen(boolean open, String title);
}
