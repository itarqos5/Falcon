package com.example.content;

import net.fabricmc.api.ClientModInitializer;
import net.fabricmc.api.ModInitializer;
import net.fabricmc.fabric.api.client.event.lifecycle.v1.ClientTickEvents;
import net.fabricmc.fabric.api.client.keybinding.v1.KeyBindingHelper;
import net.fabricmc.fabric.api.client.rendering.v1.HudRenderCallback;
import net.fabricmc.fabric.api.client.screen.v1.ScreenEvents;
import net.fabricmc.fabric.api.client.screen.v1.Screens;
import net.fabricmc.fabric.api.itemgroup.v1.ItemGroupEvents;
import net.minecraft.client.KeyMapping;
import net.minecraft.client.Minecraft;
import net.minecraft.client.gui.components.Button;
import net.minecraft.client.gui.screens.TitleScreen;
import net.minecraft.core.Registry;
import net.minecraft.core.registries.BuiltInRegistries;
import net.minecraft.network.chat.Component;
import net.minecraft.resources.Identifier;
import net.minecraft.world.food.FoodProperties;
import net.minecraft.world.item.BlockItem;
import net.minecraft.world.item.CreativeModeTabs;
import net.minecraft.world.item.Item;
import net.minecraft.world.item.Rarity;
import net.minecraft.world.level.block.Block;
import net.minecraft.world.level.block.SoundType;
import net.minecraft.world.level.block.state.BlockBehaviour;

// A mod that adds things: a block with its item, a food, a tool, a key, a HUD and a button on the title screen.
public class ContentMod implements ModInitializer, ClientModInitializer {
    public static final String ID = "vmcontent";
    public static final Block RUBY_BLOCK = new Block(BlockBehaviour.Properties.of().strength(3.0f, 6.0f).sound(SoundType.AMETHYST_BLOCK).lightLevel(state -> 7));
    public static final Item RUBY = new Item(new Item.Properties().stacksTo(16).food(new FoodProperties.Builder().nutrition(4).saturationModifier(0.3f).build()));
    public static final Item RUBY_SWORD = new Item(new Item.Properties().durability(250).rarity(Rarity.EPIC));
    public static KeyMapping hello;
    static int ticks;
    static int pressed;

    @Override
    public void onInitialize() {
        Registry.register(BuiltInRegistries.BLOCK, Identifier.fromNamespaceAndPath(ID, "ruby_block"), RUBY_BLOCK);
        Registry.register(BuiltInRegistries.ITEM, Identifier.fromNamespaceAndPath(ID, "ruby_block"), new BlockItem(RUBY_BLOCK, new Item.Properties()));
        Registry.register(BuiltInRegistries.ITEM, Identifier.fromNamespaceAndPath(ID, "ruby"), RUBY);
        Registry.register(BuiltInRegistries.ITEM, Identifier.fromNamespaceAndPath(ID, "ruby_sword"), RUBY_SWORD);
        ItemGroupEvents.modifyEntriesEvent(CreativeModeTabs.INGREDIENTS).register(entries -> entries.accept(RUBY));
        ItemGroupEvents.modifyEntriesEvent(CreativeModeTabs.TOOLS_AND_UTILITIES).register(entries -> entries.accept(RUBY_SWORD));
    }

    @Override
    public void onInitializeClient() {
        hello = KeyBindingHelper.registerKeyBinding(new KeyMapping("key.vmcontent.hello", 72, "key.categories.vmcontent"));
        ClientTickEvents.END_CLIENT_TICK.register(client -> {
            ticks++;
            while (hello.consumeClick()) client.player.displayClientMessage(Component.literal("the key was pressed " + (++pressed)), false);
        });
        HudRenderCallback.EVENT.register((graphics, delta) -> {
            Minecraft client = Minecraft.getInstance();
            graphics.fill(4, 4, 96, 16, 0x90000000);
            graphics.drawString(client.font, "Ruby HUD " + ticks, 8, 6, 0xFFFF5555);
        });
        ScreenEvents.AFTER_INIT.register((client, screen, width, height) -> {
            if (screen instanceof TitleScreen) {
                Screens.getButtons(screen).add(Button.builder(Component.literal("Ruby Button"), b -> client.player.displayClientMessage(Component.literal("the ruby button was pressed"), false))
                    .bounds(width / 2 + 104, height / 4 + 48, 98, 20).build());
            }
        });
    }
}
