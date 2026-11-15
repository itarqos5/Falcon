// Client tests: options, world.
#include "test_support.hpp"

void test_game_options_round_trip() {
    falcon::GameOptions defaults;
    CHECK(defaults.modelParts[0] && defaults.mainHandRight && defaults.chatColors && defaults.bobView);
    CHECK(defaults.textBackgroundOpacity == 0.5 && defaults.chatHeightUnfocused == 0.4375 && defaults.glintStrength == 0.75);
    CHECK(defaults.sharePresence == falcon::PresenceSharing::All && !defaults.highContrast && defaults.menuBackgroundBlurriness == 5);

    falcon::GameOptions changed;
    changed.fov = 95;
    changed.chatVisibility = falcon::ChatVisibility::Hidden;
    changed.modelParts[6] = false;
    changed.mainHandRight = false;
    changed.chatDelay = 1.5;
    changed.panoramaSpeed = 0.25;
    changed.resourcePacks = {"vanilla", "high_contrast", "file/My Pack.zip"};
    changed.audioDevice = "Speakers (Realtek)";
    changed.setVolume(falcon::SoundSource::Music, 0.3);
    falcon::OptionsFile file;
    changed.save(file);

    falcon::GameOptions back;
    back.load(falcon::OptionsFile::parse(file.toString()));
    CHECK(back.fov == 95 && back.chatVisibility == falcon::ChatVisibility::Hidden && !back.modelParts[6] && back.modelParts[0]);
    CHECK(!back.mainHandRight && near(static_cast<float>(back.chatDelay), 1.5f) && near(static_cast<float>(back.panoramaSpeed), 0.25f));
    CHECK((back.resourcePacks == std::vector<std::string>{"vanilla", "high_contrast", "file/My Pack.zip"}));
    CHECK(back.audioDevice == "Speakers (Realtek)" && near(static_cast<float>(back.volume(falcon::SoundSource::Music)), 0.3f));

    // Damaged values keep the defaults instead of breaking anything.
    falcon::GameOptions safe;
    safe.load(falcon::OptionsFile::parse("fov:9999\nchatVisibility:7\nchatDelay:-3\nglintSpeed:abc\nmainHand:left\nlang:\n"));
    CHECK(safe.language == "en_us");  // an empty language code keeps English
    CHECK(safe.fov == 110 && safe.chatVisibility == falcon::ChatVisibility::Full && safe.chatDelay == 0.0);
    CHECK(safe.glintSpeed == 0.5 && !safe.mainHandRight);
}

void test_select_world_layout() {
    using falcon::Rect;
    // 854x480 at scale 2 -> 427x240 GUI units, "Select World" is 60 wide.
    const auto l = falcon::computeSelectWorldLayout(427, 240, 60);
    CHECK((l.search == Rect{113, 21, 200, 20}));
    CHECK(l.title.y == 8 && l.title.x == 113 + (200 - 60) / 2);
    CHECK((l.list == Rect{0, 49, 427, 131}));
    CHECK((l.play == Rect{59, 188, 150, 20}) && (l.create == Rect{217, 188, 150, 20}));
    CHECK((l.edit == Rect{59, 212, 71, 20}) && (l.remove == Rect{138, 212, 71, 20}));
    CHECK((l.recreate == Rect{217, 212, 71, 20}) && (l.back == Rect{296, 212, 71, 20}));

    falcon::LevelSummary s;
    s.id = "My_World-2";
    s.levelName = "Castle Build";
    CHECK(falcon::worldMatchesFilter(s, "") && falcon::worldMatchesFilter(s, "castle") && falcon::worldMatchesFilter(s, "WORLD"));
    CHECK(!falcon::worldMatchesFilter(s, "nether"));  // matches the name or the folder name, nothing else
}

void test_level_summary() {
    namespace fs = std::filesystem;
    const fs::path base = fs::temp_directory_path() / "falcon_worlds_test";
    std::error_code ignored;
    fs::remove_all(base, ignored);
    const fs::path saves = base / "saves";

    const auto str = [](const std::string& k, const std::string& v) { std::vector<unsigned char> o; nbtHead(o, 8, k); nbtName(o, v); return o; };
    const auto num = [](const std::string& k, int type, unsigned long long v, int bytes) { std::vector<unsigned char> o; nbtHead(o, type, k); nbtBE(o, v, bytes); return o; };
    const auto cat = [](std::vector<std::vector<unsigned char>> parts) { std::vector<unsigned char> o; for (auto& p : parts) o.insert(o.end(), p.begin(), p.end()); return o; };
    const auto versionTag = [&](const std::string& name, int id, const std::string& series) {
        std::vector<unsigned char> o;
        nbtHead(o, 10, "Version");
        for (const auto& part : {str("Name", name), num("Id", 3, static_cast<unsigned>(id), 4), str("Series", series)}) o.insert(o.end(), part.begin(), part.end());
        o.push_back(0);
        return o;
    };

    makeWorld(saves, "Alpha", cat({num("version", 3, 19133, 4), str("LevelName", "Alpha World"), num("LastPlayed", 4, 2000, 8),
                                   num("GameType", 3, 1, 4), num("allowCommands", 1, 1, 1), num("DataVersion", 3, 5023, 4),
                                   versionTag("26.3", 5023, "main")}));
    std::vector<unsigned char> difficulty;  // difficulty_settings { difficulty, hardcore, locked }
    nbtHead(difficulty, 10, "difficulty_settings");
    for (const auto& part : {str("difficulty", "hard"), num("hardcore", 1, 1, 1), num("locked", 1, 0, 1)}) difficulty.insert(difficulty.end(), part.begin(), part.end());
    difficulty.push_back(0);
    makeWorld(saves, "beta", cat({num("version", 3, 19133, 4), num("LastPlayed", 4, 1000, 8), num("DataVersion", 3, 5030, 4), difficulty,
                                  versionTag("26.9", 5030, "main")}));
    makeWorld(saves, "other", cat({num("version", 3, 19133, 4), num("LastPlayed", 4, 500, 8), num("DataVersion", 3, 5023, 4), versionTag("1.0", 5023, "legacy")}));
    makeWorld(saves, "nolock", cat({num("version", 3, 19133, 4)}), /*lock=*/false);       // dropped: no session.lock
    writeBytes(saves / "broken" / "level.dat", {9, 9, 9});                                // corrupted
    writeBytes(saves / "broken" / "session.lock", {1});
    fs::create_directories(saves / "not-a-world");                                        // no level.dat: not a candidate

    const auto worlds = falcon::scanWorlds(saves);
    CHECK(worlds.has_value());
    if (!worlds) return;
    CHECK(worlds->size() == 4);  // nolock and not-a-world are not listed
    if (worlds->size() == 4) {
        // Newest first: the corrupted folder uses its file time (now), then 2000, 1000, 500.
        CHECK((*worlds)[0].kind == falcon::WorldKind::Corrupted && (*worlds)[0].id == "broken");
        const falcon::LevelSummary& a = (*worlds)[1];
        const falcon::LevelSummary& b = (*worlds)[2];
        const falcon::LevelSummary& other = (*worlds)[3];
        CHECK(a.id == "Alpha" && a.displayName() == "Alpha World" && a.gameType == 1 && a.allowCommands && !a.hardcore);
        CHECK(!a.requiresFileFixing && a.backup == falcon::BackupStatus::None && a.compatible && a.primaryActionActive() && a.canEdit());
        CHECK(b.id == "beta" && b.displayName() == "beta" && b.hardcore && b.backup == falcon::BackupStatus::Downgrade);  // empty LevelName -> folder
        CHECK(other.id == "other" && !other.compatible && !other.primaryActionActive() && !other.canEdit() && other.canDelete());
        CHECK(a.primaryActionKey() == "selectWorld.select" && (*worlds)[0].primaryActionKey() == "recover_world.button");
    }

    // The three info lines, composed from the language file exactly as the game does.
    const fs::path assets = base / "assets";
    writeText(assets / "minecraft" / "lang" / "en_us.json",
              R"({"gameMode.creative":"Creative Mode","gameMode.hardcore":"Hardcore Mode","selectWorld.commands":"Commands",)"
              R"("selectWorld.version":"Version:","selectWorld.incompatible.info":"Incompatible version: %s"})");
    const falcon::ResourceManager resources(assets);
    const falcon::Language lang = falcon::Language::load(resources, "en_us");
    const auto joined = [&](const falcon::LevelSummary& s) { std::string t; for (const auto& seg : falcon::levelInfoSegments(s, lang)) t += seg.text; return t; };
    if (worlds->size() == 4) {
        CHECK(joined((*worlds)[1]) == "Creative Mode,Commands,Version: 26.3");   // no space after the commas
        const auto hardSegments = falcon::levelInfoSegments((*worlds)[2], lang);
        CHECK(hardSegments.size() == 3 && hardSegments[0].text == "Hardcore Mode" && hardSegments[0].color == 0xFFFF0000u);
        CHECK(hardSegments.back().text == "26.9" && hardSegments.back().color == 0xFFFF5555u);  // downgrade: red version
        CHECK(joined((*worlds)[3]) == "Incompatible version: 1.0");
        CHECK(falcon::levelIdLine((*worlds)[1]).rfind("Alpha (", 0) == 0);          // "folder (date)"
    }
    falcon::LevelSummary symlink;
    symlink.kind = falcon::WorldKind::Symlink;
    symlink.id = "x";
    symlink.lastPlayed = -1;
    CHECK(falcon::levelIdLine(symlink) == "x");

    // Deleting removes only a real folder directly inside saves/.
    CHECK(!falcon::deleteWorld(saves, "../assets") && !falcon::deleteWorld(saves, "") && !falcon::deleteWorld(saves, "..") && fs::exists(assets));
    CHECK(!falcon::deleteWorld(saves, "Alpha/level.dat") && fs::exists(saves / "Alpha" / "level.dat"));
    CHECK(falcon::deleteWorld(saves, "Alpha") && !fs::exists(saves / "Alpha"));
    CHECK(!falcon::deleteWorld(saves, "Alpha"));                                     // already gone
    CHECK(falcon::scanWorlds(saves)->size() == 3);
    fs::remove_all(base, ignored);
}

void test_resource_layers() {
    namespace fs = std::filesystem;
    using falcon::ResourceLocation;
    const fs::path base = fs::temp_directory_path() / "falcon_layers_test";
    std::error_code ignored;
    fs::remove_all(base, ignored);

    // The game's own assets, a folder pack and a zip pack (opened from a real file, so it is mapped).
    writeText(base / "assets" / "minecraft" / "a.txt", "base-a");
    writeText(base / "assets" / "minecraft" / "b.txt", "base-b");
    writeText(base / "assets" / "minecraft" / "lang" / "en_us.json", R"({"k1":"base1","k2":"base2"})");
    writeText(base / "assets" / "minecraft" / "sounds.json",
              R"({"ev.x":{"sounds":["base_x"]},"ev.y":{"sounds":["base_y"]}})");
    writeText(base / "p1" / "assets" / "minecraft" / "a.txt", "p1-a");
    writeText(base / "p1" / "assets" / "minecraft" / "lang" / "en_us.json", R"({"k2":"p1-2","k3":"p1-3"})");
    writeText(base / "p1" / "assets" / "minecraft" / "sounds.json", R"({"ev.x":{"sounds":["p1_x"]}})");
    writeBytes(base / "p2.zip", makeZip({
        {"assets/minecraft/a.txt", "zip-a", true},
        {"assets\\minecraft\\c.txt", "zip-c", false},  // some Windows tools write backslashes
        {"assets/minecraft/sounds.json", R"({"ev.y":{"replace":true,"sounds":["z_y"]}})", false},
    }));

    {
        const auto p1 = falcon::makeDirectoryPack(base / "p1");
        auto zip = falcon::ZipArchive::open(base / "p2.zip");
        CHECK(zip.has_value());
        if (!zip) return;
        const auto p2 = falcon::makeZipPack(std::move(*zip));
        const ResourceLocation a("minecraft", "a.txt");
        const ResourceLocation b("minecraft", "b.txt");
        const ResourceLocation c("minecraft", "c.txt");

        falcon::ResourceManager resources(base / "assets");
        CHECK(resources.readText(a) == "base-a" && !resources.exists(c));
        CHECK(resources.providers(a) == std::vector<const void*>{nullptr});

        resources.setPacks({p1});
        CHECK(resources.readText(a) == "p1-a" && resources.readText(b) == "base-b");  // pack beats base
        CHECK((resources.providers(a) == std::vector<const void*>{nullptr, p1.get()}));
        CHECK((resources.providers(b) == std::vector<const void*>{nullptr}));

        resources.setPacks({p1, p2});                                  // later in the list wins
        CHECK(resources.readText(a) == "zip-a" && resources.readText(c) == "zip-c");
        CHECK((resources.readAllText(a) == std::vector<std::string>{"base-a", "p1-a", "zip-a"}));
        CHECK(resources.exists(c));

        resources.setPacks({p2, p1});                                  // so the order matters
        CHECK(resources.readText(a) == "p1-a");

        // A copy and withPacks() see the packs they were given; the original is unaffected.
        resources.setPacks({p1});
        const falcon::ResourceManager copy(resources);
        const falcon::ResourceManager other = resources.withPacks({p1, p2});
        CHECK(copy.readText(a) == "p1-a" && other.readText(a) == "zip-a" && resources.readText(a) == "p1-a");
        // Unchanged resources have identical providers, so a reload can skip them.
        CHECK(resources.providers(b) == other.providers(b) && resources.providers(a) != other.providers(a));

        // Language files merge key by key; later packs override single keys.
        resources.setPacks({p1});
        const falcon::Language lang = falcon::Language::load(resources, "en_us");
        CHECK(lang.get("k1") == "base1" && lang.get("k2") == "p1-2" && lang.get("k3") == "p1-3");

        // sounds.json: a pack adds sounds to an event, or replaces them with "replace": true.
        resources.setPacks({p1, p2});
        const falcon::SoundRegistry sounds = falcon::SoundRegistry::load(resources);
        const falcon::SoundEvent* x = sounds.find("ev.x");
        const falcon::SoundEvent* y = sounds.find("ev.y");
        CHECK(x != nullptr && x->entries.size() == 2 && x->entries[0].name == "base_x" &&
              x->entries[1].name == "p1_x");
        CHECK(y != nullptr && y->entries.size() == 1 && y->entries[0].name == "z_y");
    }
    fs::remove_all(base, ignored);  // after the zip is unmapped (Windows cannot delete a mapped file)
}

void test_pack_repository_and_model() {
    namespace fs = std::filesystem;
    const fs::path base = fs::temp_directory_path() / "falcon_pack_test";
    fs::remove_all(base);
    const fs::path assets = base / "game" / "assets";
    const fs::path gameDir = base / "dotminecraft";
    fs::create_directories(assets / "minecraft" / "lang");
    {
        std::ofstream lang(assets / "minecraft" / "lang" / "en_us.json");
        lang << R"json({"pack.nameAndSource":"%s (%s)","pack.source.builtin":"built-in",
                    "resourcePack.vanilla.name":"Default","resourcePack.vanilla.description":"Vanilla look",
                    "resourcePack.high_contrast.name":"High Contrast","resourcePack.programmer_art.name":"Programmer Art"})json";
    }
    writeBytes(assets / "minecraft" / "resourcepacks" / "high_contrast.zip",
               makeZip({{"pack.mcmeta", R"({"pack":{"description":"Contrast","min_format":29,"max_format":98}})"}}));
    writeBytes(assets / "minecraft" / "resourcepacks" / "programmer_art.zip",
               makeZip({{"pack.mcmeta", R"({"pack":{"description":"Classic","min_format":97,"max_format":98}})"}}));
    // A folder pack made for an older version, in <game dir>/resourcepacks.
    fs::create_directories(gameDir / "resourcepacks" / "Old Pack");
    std::ofstream(gameDir / "resourcepacks" / "Old Pack" / "pack.mcmeta")
        << R"({"pack":{"pack_format":5,"description":{"text":"Ancient"}}})";

    const falcon::ResourceManager resources(assets);
    const falcon::Language language = falcon::Language::load(resources, "en_us");
    const auto repo = falcon::PackRepository::load(language, assets, gameDir);

    // Packs are keyed by id: file/..., high_contrast, programmer_art, vanilla.
    std::vector<std::string> ids;
    for (const auto& [id, pack] : repo.packs()) ids.push_back(id);
    CHECK((ids == std::vector<std::string>{"file/Old Pack", "high_contrast", "programmer_art", "vanilla"}));
    CHECK(repo.find("vanilla")->description == "Vanilla look (built-in)" && repo.find("vanilla")->required);
    CHECK(repo.find("high_contrast")->description == "Contrast (built-in)");
    CHECK(repo.find("high_contrast")->compatibility == falcon::PackCompatibility::Compatible);
    CHECK(repo.find("file/Old Pack")->compatibility == falcon::PackCompatibility::TooOld);
    CHECK(repo.find("file/Old Pack")->description == "Ancient");
    CHECK(falcon::PackRepository::checkFormat(R"({"pack":{"pack_format":97}})") == falcon::PackCompatibility::Compatible);
    CHECK(falcon::PackRepository::checkFormat(R"({"pack":{"min_format":98,"max_format":99}})") == falcon::PackCompatibility::TooNew);
    CHECK(falcon::PackRepository::checkFormat("{}") == falcon::PackCompatibility::Broken);

    // Selection rules.
    falcon::PackSelectionModel model(repo, {});
    CHECK((model.selected() == std::vector<std::string>{"vanilla"}));
    CHECK((model.available() == std::vector<std::string>{"file/Old Pack", "high_contrast", "programmer_art"}));
    CHECK(!model.canUnselect("vanilla") && !model.canMoveUp("vanilla") && !model.canMoveDown("vanilla"));
    CHECK(model.canSelect("high_contrast") && !model.canSelect("vanilla"));

    model.select("high_contrast");  // new packs go on top of Selected
    model.select("programmer_art");
    CHECK((model.selected() == std::vector<std::string>{"programmer_art", "high_contrast", "vanilla"}));
    CHECK(!model.canMoveUp("programmer_art") && model.canMoveDown("programmer_art"));
    CHECK(model.canMoveUp("vanilla") && !model.canMoveDown("vanilla"));
    model.moveDown("programmer_art");
    CHECK((model.selected() == std::vector<std::string>{"high_contrast", "programmer_art", "vanilla"}));
    CHECK((model.commitIds() == std::vector<std::string>{"vanilla", "programmer_art", "high_contrast"}));

    model.unselect("high_contrast");  // unselected packs go on top of Available
    CHECK((model.available().front() == "high_contrast"));
    model.unselect("vanilla");        // required: ignored
    CHECK(model.isSelected("vanilla"));

    // Restoring saved ids (lowest priority first) rebuilds the same order.
    falcon::PackSelectionModel restored(repo, {"vanilla", "programmer_art", "high_contrast"});
    CHECK((restored.selected() == std::vector<std::string>{"high_contrast", "programmer_art", "vanilla"}));
    CHECK((restored.available() == std::vector<std::string>{"file/Old Pack"}));
    fs::remove_all(base);
}

class FixedEntry : public falcon::ScrollList::Entry {
public:
    explicit FixedEntry(int h) : Entry(h) {}
    void render(falcon::GuiGraphics&, const falcon::Rect&, int, int, const falcon::ScrollList::RowState&) override {}
};

void test_scroll_list() {
    using falcon::ScrollList;
    // The Music & Sounds list at 427x240: 9 rows of 25 in a 174px viewport.
    ScrollList list(0, 33, 427, 174, 25, 310, ScrollList::ScrollbarPlacement::RightOfRow, false, false);
    for (int i = 0; i < 9; ++i) list.addEntry<FixedEntry>(25);
    CHECK(list.contentHeight() == 229 && list.maxScroll() == 55 && list.scrollable());
    CHECK(list.scrollerHeight() == 132);          // (int)(174*174/229)
    CHECK(list.rowLeft() == 58 && list.scrollbarX() == 376);
    CHECK(list.entryRect(0).y == 35 && list.entryRect(1).y == 60 && list.entryRect(1).width == 310);

    CHECK(list.mouseScrolled(100.0, 100.0, -1.0));  // one notch down = 12px (25 / 2)
    CHECK(near(static_cast<float>(list.scrollAmount()), 12.0f) && list.entryRect(0).y == 23);
    CHECK(!list.mouseScrolled(100.0, 5.0, -1.0));    // over the header: not ours
    for (int i = 0; i < 10; ++i) (void)list.mouseScrolled(100.0, 100.0, -1.0);
    CHECK(near(static_cast<float>(list.scrollAmount()), 55.0f));  // clamped to the maximum
    for (int i = 0; i < 10; ++i) (void)list.mouseScrolled(100.0, 100.0, 1.0);
    CHECK(list.scrollAmount() == 0.0);

    // Everything fits: no scrolling, no scrollbar.
    ScrollList roomy(0, 33, 427, 300, 25, 310, ScrollList::ScrollbarPlacement::RightOfRow, false, false);
    for (int i = 0; i < 9; ++i) roomy.addEntry<FixedEntry>(25);
    CHECK(!roomy.scrollable() && roomy.maxScroll() == 0);

    // Pack lists: 36px rows, rows narrow by 6px while the scrollbar shows, bar inside the right edge.
    ScrollList packs(-2, 49, 200, 158, 36, 0, ScrollList::ScrollbarPlacement::InsideRight, true, true);
    for (int i = 0; i < 4; ++i) packs.addEntry<FixedEntry>(36);
    CHECK(!packs.scrollable() && packs.entryWidth() == 196 && packs.rowLeft() == 0);
    packs.addEntry<FixedEntry>(36);
    CHECK(packs.scrollable() && packs.entryWidth() == 190 && packs.scrollbarX() == -2 + 200 - 6);
    CHECK(packs.mouseClicked(50.0, 60.0, 0) && packs.selectedIndex() == 0);   // a plain click selects
    CHECK(!packs.mouseClicked(50.0, 5.0, 0));                                  // outside the list
}

void test_cycle_button() {
    bool shift = false;
    int changes = 0;
    falcon::CycleButton button(0, 0, 150, 20, 3, 0, [](int i) { return "value " + std::to_string(i); },
                             [&] { return shift; });
    button.onChange = [&](int) { ++changes; };
    CHECK(button.index() == 0);
    CHECK(button.mouseClicked(10.0, 10.0, 0) && button.index() == 1);
    CHECK(button.mouseClicked(10.0, 10.0, 0) && button.index() == 2);
    CHECK(button.mouseClicked(10.0, 10.0, 0) && button.index() == 0);  // wraps
    shift = true;
    CHECK(button.mouseClicked(10.0, 10.0, 0) && button.index() == 2);  // shift+click goes back
    CHECK(changes == 4);
    CHECK(!button.mouseClicked(10.0, 10.0, 1));                        // right click does nothing
    CHECK(button.clicksOnPress());

    // One value: pressing does nothing at all (no change, no callback, no click sound).
    changes = 0;
    button.setValues(1, 0);
    CHECK(button.valueCount() == 1 && button.index() == 0 && !button.clicksOnPress());
    CHECK(button.mouseClicked(10.0, 10.0, 0) && button.index() == 0 && changes == 0);

    // The list can grow later (the device list arrives in the background); the index is kept.
    shift = false;
    button.setValues(3, 2);
    CHECK(button.valueCount() == 3 && button.index() == 2 && button.clicksOnPress() && changes == 0);
    CHECK(button.mouseClicked(10.0, 10.0, 0) && button.index() == 0 && changes == 1);
    button.setValues(2, 9);  // an out-of-range index is clamped
    CHECK(button.index() == 1);
}

void test_audio_devices() {
    using falcon::AudioDevice;
    const AudioDevice speakers{"Speakers (Realtek)", true};
    const AudioDevice headset{"Headset (Plantronics)", false};

    // Nothing to choose between with zero or one device: only "System Default" is offered.
    CHECK((falcon::buildDeviceChoices({}) == std::vector<std::string>{""}));
    CHECK((falcon::buildDeviceChoices({speakers}) == std::vector<std::string>{""}));
    // Two or more: System Default first, then every device in the OS order.
    CHECK((falcon::buildDeviceChoices({speakers, headset}) ==
           std::vector<std::string>{"", "Speakers (Realtek)", "Headset (Plantronics)"}));

    // An offline engine keeps an injected list and bumps its version so screens notice.
    falcon::AudioEngine engine(falcon::AudioEngine::Mode::Offline, 48000);
    CHECK(engine.devices().empty());
    const unsigned version = engine.deviceListVersion();
    engine.setDevicesForTest({speakers, headset});
    CHECK(engine.devices().size() == 2 && engine.deviceListVersion() != version);
    engine.selectDevice("Headset (Plantronics)");
    CHECK(engine.deviceName() == "Headset (Plantronics)");
    engine.selectDevice("");
    CHECK(engine.deviceName() == "offline");
}

void test_splash_dates() {
    using falcon::SplashManager;
    CHECK(SplashManager::specialSplash(12, 24) == "Merry X-mas!");
    CHECK(SplashManager::specialSplash(1, 1) == "Happy new year!");
    CHECK(SplashManager::specialSplash(10, 31) == "OOoooOOOoooo! Spooky!");
    CHECK(!SplashManager::specialSplash(11, 12).has_value());
}

void test_widget_clicks() {
    int pressed = 0;
    falcon::Button button("Singleplayer", 113, 108, 200, 20);
    button.onPress = [&] { ++pressed; };

    CHECK(button.mouseClicked(113.0, 108.0, 0));    // top-left pixel is inside
    CHECK(button.mouseClicked(312.9, 127.9, 0));    // bottom-right pixel is inside
    CHECK(!button.mouseClicked(313.0, 118.0, 0));   // right edge is exclusive
    CHECK(!button.mouseClicked(112.9, 118.0, 0));
    CHECK(!button.mouseClicked(200.0, 118.0, 1));   // only the left mouse button presses
    button.active = false;
    CHECK(!button.mouseClicked(200.0, 118.0, 0));   // disabled buttons ignore clicks
    CHECK(pressed == 2);

    falcon::IconButton icon("icon/language", 15, 15, 203, 180, 20);
    CHECK(icon.width == 20 && icon.height == 20);
    CHECK(icon.contains(203.0, 180.0) && !icon.contains(223.0, 180.0));
}

void test_asset_index() {
    namespace fs = std::filesystem;
    const fs::path dir = fs::temp_directory_path() / "falcon_asset_index_test";
    fs::remove_all(dir);
    fs::create_directories(dir / "indexes");
    fs::create_directories(dir / "objects" / "ab");

    const auto write = [](const fs::path& file, const std::string& text) {
        std::ofstream(file, std::ios::binary) << text;
    };
    // Older index lacks the asset; the highest-numbered one (not the lexically last) wins.
    write(dir / "indexes" / "9.json", R"({"objects":{}})");
    write(dir / "indexes" / "12.json",
          R"({"objects":{"icons/icon_16x16.png":{"hash":"abcdef0123","size":5}}})");
    write(dir / "objects" / "ab" / "abcdef0123", "hello");

    const auto index = falcon::AssetIndex::open(dir);
    CHECK(index.has_value());
    if (index) {
        CHECK(index->indexId() == "12");
        CHECK(index->contains("icons/icon_16x16.png"));
        CHECK(!index->contains("icons/icon_32x32.png"));
        const auto bytes = index->read("icons/icon_16x16.png");
        CHECK(bytes && std::string(bytes->begin(), bytes->end()) == "hello");
        CHECK(!index->read("missing.png").has_value());
    }
    CHECK(!falcon::AssetIndex::open(dir / "nope").has_value());
    fs::remove_all(dir);
}

void test_edition_badge() {
    // Wrong size: left alone.
    falcon::Image tiny;
    tiny.width = 16;
    tiny.height = 16;
    tiny.rgba.assign(16 * 16 * 4, 0);
    CHECK(!falcon::makeCppEditionBadge(tiny).has_value());

    // Right size but no lettering: left alone.
    falcon::Image empty;
    empty.width = 512;
    empty.height = 64;
    empty.rgba.assign(512 * 64 * 4, 0);
    CHECK(!falcon::makeCppEditionBadge(empty).has_value());

    // A stand-in with just the pixels the generator probes and samples: it runs and draws the plus.
    const auto set = [&](falcon::Image& img, int x, int y, unsigned char v) {
        const std::size_t i = (static_cast<std::size_t>(y) * 512 + static_cast<std::size_t>(x)) * 4;
        img.rgba[i] = img.rgba[i + 1] = img.rgba[i + 2] = v;
        img.rgba[i + 3] = 255;
    };
    falcon::Image fake = empty;
    for (const auto& p : {std::pair{215, 8}, std::pair{240, 8}, std::pair{212, 24}, std::pair{240, 38},
                          std::pair{230, 8}, std::pair{230, 4}, std::pair{230, 13}, std::pair{209, 8},
                          std::pair{249, 8}}) {
        set(fake, p.first, p.second, 208);
    }
    for (int row = 43; row <= 50; ++row) set(fake, 230, row, 60);
    set(fake, 242, 26, 0);
    const auto badge = falcon::makeCppEditionBadge(fake);
    CHECK(badge.has_value());
    if (badge) {
        CHECK(badge->width == 512 && badge->height == 64);
        CHECK(badge->alphaAt(100, 23) == 255);  // centre of the first plus
    }

    // Missing colour samples (transparent E) must be rejected rather than draw invisible pluses.
    falcon::Image hollow = fake;
    hollow.rgba[(static_cast<std::size_t>(4) * 512 + 230) * 4 + 3] = 0;
    CHECK(!falcon::makeCppEditionBadge(hollow).has_value());
}

void test_text_util() {
    CHECK(falcon::javaStringHash("hello") == 99162322);  // "hello".hashCode() in Java
    CHECK(falcon::javaStringHash("") == 0);
    CHECK(falcon::javaTrim("  hi \n") == "hi");
    const auto cps = falcon::decodeUtf8("a\xC3\xA9\xE2\x82\xAC");  // a, é, euro sign
    CHECK(cps.size() == 3 && cps[1] == 0xE9 && cps[2] == 0x20AC);
}

void test_math() {
    CHECK(near(falcon::clampedMap(0.75f, 0.5f, 1.0f, 0.0f, 1.0f), 0.5f));
    CHECK(near(falcon::clampedMap(0.2f, 0.5f, 1.0f, 0.0f, 1.0f), 0.0f));
    CHECK(near(falcon::clampedMap(2.0f, 0.5f, 1.0f, 0.0f, 1.0f), 1.0f));
    CHECK(near(falcon::wrapDegrees(190.0f), -170.0f));
    CHECK(near(falcon::wrapDegrees(-190.0f), 170.0f));
    CHECK(near(falcon::wrapDegrees(180.0f), -180.0f));

    // translate -> rotate -> scale composition (post-multiplied), as the splash text uses it.
    falcon::Pose2D pose;
    pose.translate(10.0f, 20.0f).rotate(falcon::kPi / 2.0f).scale(2.0f);
    float x = 0.0f;
    float y = 0.0f;
    pose.apply(1.0f, 0.0f, x, y);  // (1,0) -> scaled (2,0) -> rotated 90deg (0,2) -> moved (10,22)
    CHECK(near(x, 10.0f) && near(y, 22.0f));
}


void test_nbt_writer() {
    using falcon::NbtTag;
    NbtTag inner = NbtTag::compound();
    inner.put("Name", NbtTag::ofString("26.3")).put("Id", NbtTag::ofInteger(NbtTag::Type::Int, 5023));
    NbtTag data = NbtTag::compound();
    data.put("LevelName", NbtTag::ofString("caf\xC3\xA9 \xF0\x9F\x8C\x8D"))  // an accent and an emoji (above U+FFFF)
        .put("Nul", NbtTag::ofString(std::string("a\0b", 3)))
        .put("Short", NbtTag::ofInteger(NbtTag::Type::Short, -2))
        .put("Hardcore", NbtTag::ofBool(true))
        .put("LastPlayed", NbtTag::ofInteger(NbtTag::Type::Long, 1700000000123LL))
        .put("Negative", NbtTag::ofInteger(NbtTag::Type::Int, -7))
        .put("Version", inner)
        .put("Tags", NbtTag::ofList({NbtTag::ofString("a"), NbtTag::ofString("b")}))
        .put("EmptyList", NbtTag::ofList({}));
    NbtTag root = NbtTag::compound();
    root.put("Data", data);

    const auto raw = falcon::serializeNbt(root);
    for (const auto& bytes : {raw, falcon::gzipStored(raw)}) {
        const auto parsed = falcon::parseNbt(bytes);
        CHECK(parsed.has_value());
        if (!parsed) continue;
        const NbtTag* d = parsed->get("Data");
        CHECK(d != nullptr);
        if (d == nullptr) continue;
        CHECK(d->getString("LevelName") == "caf\xC3\xA9 \xF0\x9F\x8C\x8D");  // survives the modified UTF-8 detour
        CHECK(d->getString("Nul") == std::string("a\0b", 3));
        CHECK(d->getInt("Short") == -2 && d->getBool("Hardcore") && d->getInt("Negative") == -7);
        CHECK(d->getInt("LastPlayed") == 1700000000123LL);
        CHECK(d->get("Version") != nullptr && d->get("Version")->getInt("Id") == 5023);
        CHECK(d->get("Tags") != nullptr && d->get("Tags")->items.size() == 2 && d->get("Tags")->items[1].text == "b");
        CHECK(d->get("EmptyList") != nullptr && d->get("EmptyList")->items.empty());
    }
    // The emoji really is written as two three-byte surrogates (what Java expects), not four bytes.
    const std::vector<unsigned char> needle = {0xED, 0xA0, 0xBC, 0xED, 0xBC, 0x8D};  // U+1F30D as the surrogates D83C DF0D
    CHECK(std::search(raw.begin(), raw.end(), needle.begin(), needle.end()) != raw.end());

    // A payload larger than one stored deflate block (65535 bytes) still reads back.
    NbtTag big = NbtTag::compound();
    NbtTag longs;
    longs.type = NbtTag::Type::LongArray;
    for (int i = 0; i < 20000; ++i) longs.numbers.push_back(static_cast<std::int64_t>(i) * 1000003);  // 160 KB
    big.put("Longs", longs);
    const auto gz = falcon::gzipStored(falcon::serializeNbt(big));
    const auto back = falcon::parseNbt(gz);
    CHECK(back.has_value() && back->get("Longs") != nullptr && back->get("Longs")->numbers.size() == 20000 &&
          back->get("Longs")->numbers[19999] == 19999LL * 1000003);
    const auto empty = falcon::gzipStored({});
    CHECK(falcon::gunzip(empty.data(), empty.size()).has_value());  // empty input is a valid gzip stream too
}

void test_create_world_layout() {
    using falcon::Rect;
    // The spec's worked example: 854x480 at GUI scale 2 is 427x240.
    const auto l = falcon::computeCreateWorldLayout(427, 240);
    CHECK(l.tabX == 28 && l.tabWidth == 124);
    CHECK((l.create == Rect{59, 214, 150, 20}) && (l.cancel == Rect{217, 214, 150, 20}));
    CHECK(l.footerSeparatorY == 205);
    CHECK((l.tabArea == Rect{0, 24, 427, 183}));
    CHECK((falcon::createWorldTabGrid(l, 210, 117) == Rect{108, 35, 210, 117}));  // Game: name label at (109, 35)
    CHECK((falcon::createWorldTabGrid(l, 310, 117) == Rect{58, 35, 310, 117}));   // World
    CHECK((falcon::createWorldTabGrid(l, 210, 76) == Rect{108, 41, 210, 76}));    // More

    const auto big = falcon::computeCreateWorldLayout(480, 270);
    CHECK(big.tabX == 54 && big.tabWidth == 124);
    CHECK((big.create == Rect{86, 244, 150, 20}) && (big.cancel.x == 244));
    const auto g = falcon::createWorldTabGrid(big, 210, 117);
    CHECK(g.x == 135 && g.y == 40);

    const auto narrow = falcon::computeCreateWorldLayout(300, 200);  // the tab bar shrinks below 400 + margins
    CHECK(narrow.tabX == 14 && narrow.tabWidth == 90);
}

void test_new_world() {
    namespace fs = std::filesystem;
    const fs::path base = fs::temp_directory_path() / "falcon_new_world_test";
    std::error_code ignored;
    fs::remove_all(base, ignored);
    const fs::path saves = base / "saves";
    fs::create_directories(saves);

    using falcon::findAvailableWorldFolder;
    CHECK(findAvailableWorldFolder(saves, "New World", "New World") == "New World");
    CHECK(findAvailableWorldFolder(saves, "   ", "New World") == "New World");   // blank: the default name
    CHECK(findAvailableWorldFolder(saves, "a:b?c.d", "x") == "a_b_c_d");         // illegal characters and dots
    CHECK(findAvailableWorldFolder(saves, "CON", "x") == "_CON_");               // a reserved Windows name
    CHECK(findAvailableWorldFolder(saves, "lpt3", "x") == "_lpt3_");
    CHECK(findAvailableWorldFolder(saves, "  Padded  ", "x") == "Padded");
    CHECK(findAvailableWorldFolder(saves, "World ()", "x") == "World");          // an empty counter makes the game fall back
    fs::create_directories(saves / "New World");
    CHECK(findAvailableWorldFolder(saves, "New World", "x") == "New World (1)");
    fs::create_directories(saves / "New World (1)");
    CHECK(findAvailableWorldFolder(saves, "New World", "x") == "New World (2)");
    CHECK(findAvailableWorldFolder(saves, "New World (1)", "x") == "New World (2)");  // keeps counting from the (1)
    CHECK(findAvailableWorldFolder(saves, "Other (7)", "x") == "Other (7)");          // a counter that is free is kept

    std::mt19937_64 rng(1);
    CHECK(falcon::parseWorldSeed("42", rng) == 42 && falcon::parseWorldSeed(" -7 ", rng) == -7 && falcon::parseWorldSeed("+5", rng) == 5);
    CHECK(falcon::parseWorldSeed("9223372036854775807", rng) == INT64_MAX);
    CHECK(falcon::parseWorldSeed("-9223372036854775808", rng) == INT64_MIN);
    CHECK(falcon::parseWorldSeed("9223372036854775808", rng) == falcon::javaStringHash("9223372036854775808"));  // too big: hashed
    CHECK(falcon::parseWorldSeed("abc", rng) == 96354 && falcon::parseWorldSeed("-", rng) == falcon::javaStringHash("-"));
    CHECK(falcon::parseWorldSeed("", rng) != falcon::parseWorldSeed("  ", rng));  // blank: random, and different each time

    // Creating a world makes a folder the Singleplayer list reads back.
    falcon::NewWorldSettings s;
    s.name = "  Creative Test  ";
    s.mode = falcon::NewWorldMode::Creative;
    std::string error;
    CHECK(falcon::createWorldFolder(saves, "Creative Test", s, 1234, 5000, error));
    falcon::NewWorldSettings hard;
    hard.name = "Hardcore Test";
    hard.mode = falcon::NewWorldMode::Hardcore;
    hard.allowCommands = true;  // ignored: hardcore never has cheats
    CHECK(falcon::createWorldFolder(saves, "Hardcore Test", hard, 1, 9000, error));
    CHECK(!falcon::createWorldFolder(saves, "Hardcore Test", s, 1, 9000, error) && !error.empty());  // never reuses a folder

    const auto worlds = falcon::scanWorlds(saves);
    CHECK(worlds.has_value());
    if (worlds) {
        const falcon::LevelSummary* creative = nullptr;
        const falcon::LevelSummary* hardcore = nullptr;
        for (const auto& w : *worlds) {
            if (w.id == "Creative Test") creative = &w;
            if (w.id == "Hardcore Test") hardcore = &w;
        }
        CHECK(creative != nullptr && hardcore != nullptr);
        if (creative != nullptr && hardcore != nullptr) {
            CHECK(creative->kind == falcon::WorldKind::Normal && creative->levelName == "Creative Test");  // trimmed
            CHECK(creative->gameType == 1 && creative->allowCommands && !creative->hardcore);            // creative: cheats default on
            CHECK(creative->lastPlayed == 5000 && creative->versionName == "26.3" && creative->compatible);
            CHECK(!creative->requiresFileFixing && !creative->shouldBackup() && creative->primaryActionActive());
            CHECK(hardcore->hardcore && hardcore->gameType == 0 && !hardcore->allowCommands);
            CHECK((*worlds)[0].id == "Hardcore Test");  // newest first
        }
    }
    // The seed and options are in level.dat for the day a world can be generated from them.
    const auto bytes = [&] {
        std::ifstream in(saves / "Creative Test" / "level.dat", std::ios::binary);
        return std::vector<unsigned char>(std::istreambuf_iterator<char>(in), {});
    }();
    const auto root = falcon::parseNbt(bytes);
    CHECK(root.has_value() && root->get("Data") != nullptr);
    if (root && root->get("Data") != nullptr) {
        const falcon::NbtTag* gen = root->get("Data")->get("WorldGenSettings");
        CHECK(gen != nullptr && gen->getInt("seed") == 1234 && gen->getBool("generate_features") && !gen->getBool("bonus_chest"));
        CHECK(root->get("Data")->get("difficulty_settings")->getString("difficulty") == "normal");
    }
    fs::remove_all(base, ignored);
}

void test_base64() {
    using falcon::base64Decode;
    using falcon::base64Encode;
    const auto bytes = [](std::string_view s) { return std::vector<unsigned char>(s.begin(), s.end()); };
    CHECK(base64Encode(bytes("")) == "" && base64Encode(bytes("f")) == "Zg==" && base64Encode(bytes("fo")) == "Zm8=");
    CHECK(base64Encode(bytes("foo")) == "Zm9v" && base64Encode(bytes("foobar")) == "Zm9vYmFy");
    CHECK(base64Decode("Zm9vYmE=") == bytes("fooba") && base64Decode("Zm9v\nYmFy\n") == bytes("foobar"));  // line breaks are ignored
    CHECK(base64Decode("Zg") == bytes("f"));                                                              // padding is optional
    CHECK(!base64Decode("Zm9v!") && !base64Decode("Z") && !base64Decode("Zg==Zg=="));                     // junk, one symbol, data after padding
    std::vector<unsigned char> all;
    for (int i = 0; i < 256; ++i) all.push_back(static_cast<unsigned char>(i));
    CHECK(base64Decode(base64Encode(all)) == all);
}

void test_server_address() {
    using falcon::parseServerAddress;
    const auto a = parseServerAddress("play.example.com");
    CHECK(a && a->host == "play.example.com" && a->port == 25565);
    const auto b = parseServerAddress("play.example.com:25566");
    CHECK(b && b->host == "play.example.com" && b->port == 25566);
    const auto c = parseServerAddress("[::1]:1234");
    CHECK(c && c->host == "::1" && c->port == 1234);
    const auto d = parseServerAddress("[2001:db8::1]");
    CHECK(d && d->host == "2001:db8::1" && d->port == 25565);
    const auto e = parseServerAddress("2001:db8::1");  // several colons, no brackets: all host
    CHECK(e && e->host == "2001:db8::1" && e->port == 25565);
    const auto f = parseServerAddress("host:");  // an empty port means the default
    CHECK(f && f->host == "host" && f->port == 25565);
    CHECK(parseServerAddress("host:0")->port == 0 && parseServerAddress("host:65535")->port == 65535);
    CHECK(!parseServerAddress("") && !parseServerAddress(":25565") && !parseServerAddress("host:65536"));
    CHECK(!parseServerAddress("host:12a") && !parseServerAddress("[::1") && !parseServerAddress("[::1]x") && !parseServerAddress("[]"));
    CHECK(falcon::isValidServerAddress("localhost") && !falcon::isValidServerAddress(""));
}

void test_styled_text() {
    using falcon::TextSpan;
    // Legacy formatting codes: a colour clears the styles, r resets, k is ignored.
    const auto legacy = falcon::parseLegacyFormatting("plain \xC2\xA7" "aGreen \xC2\xA7" "lBold\xC2\xA7" "r done\xC2\xA7" "kx");
    CHECK(legacy.size() == 4);
    if (legacy.size() == 4) {
        CHECK(legacy[0].text == "plain " && !legacy[0].hasColor);
        CHECK(legacy[1].text == "Green " && legacy[1].hasColor && legacy[1].rgb == 0x55FF55 && !legacy[1].bold);
        CHECK(legacy[2].text == "Bold" && legacy[2].bold && legacy[2].rgb == 0x55FF55);
        CHECK(legacy[3].text == " donex" && !legacy[3].hasColor && !legacy[3].bold);
    }
    CHECK(falcon::plainText(falcon::parseLegacyFormatting("a\xC2\xA7zb")) == "a\xC2\xA7zb");  // not a code: kept as text

    // Chat components: a plain string, an object with extra (children inherit), an array, and rubbish.
    CHECK(falcon::plainText(falcon::parseChatComponentJson("\"Hello\"")) == "Hello");
    const auto obj = falcon::parseChatComponentJson(R"({"text":"Hi ","color":"gold","extra":[{"text":"there","bold":true},"!"]})");
    CHECK(obj.size() == 3);  // the extras are siblings: "!" does not inherit the bold of "there"
    if (obj.size() == 3) {
        CHECK(obj[0].text == "Hi " && obj[0].rgb == 0xFFAA00 && !obj[0].bold);
        CHECK(obj[1].text == "there" && obj[1].rgb == 0xFFAA00 && obj[1].bold);  // inherits the colour, adds bold
        CHECK(obj[2].text == "!" && obj[2].rgb == 0xFFAA00 && !obj[2].bold);
    }
    const auto hexColor = falcon::parseChatComponentJson(R"({"text":"x","color":"#12abEF"})");
    CHECK(hexColor.size() == 1 && hexColor[0].hasColor && hexColor[0].rgb == 0x12ABEF);
    CHECK(falcon::plainText(falcon::parseChatComponentJson(R"(["a",{"text":"b"},"c"])")) == "abc");
    CHECK(falcon::plainText(falcon::parseChatComponentJson(R"({"translate":"some.key"})")) == "some.key");
    CHECK(falcon::parseChatComponentJson("{not json").empty() && falcon::parseChatComponentJson("null").empty());
    std::string deep = "\"deep\"";
    for (int i = 0; i < 40; ++i) deep = "[" + deep + "]";
    CHECK(falcon::parseChatComponentJson(deep).empty());  // deeper than 16 levels is dropped

    // Wrapping, with a font where every character is 6 wide (7 when bold).
    const falcon::TextMeasure measure = [](std::string_view t, bool bold) { return static_cast<int>(t.size()) * (bold ? 7 : 6); };
    const auto styled = falcon::parseLegacyFormatting("aaa bbb ccc ddd");
    const auto lines = falcon::wrapStyledText(measure, styled, 40, 5);  // 6 characters per line
    CHECK(lines.size() == 4 && falcon::plainText(lines[0]) == "aaa " && falcon::plainText(lines[3]) == "ddd");
    CHECK(falcon::wrapStyledText(measure, styled, 40, 2).size() == 2);  // extra lines are dropped
    const auto split = falcon::wrapStyledText(measure, falcon::parseLegacyFormatting("abcdefghijklmnop"), 36, 5);  // one 16 letter word
    CHECK(split.size() == 3 && falcon::plainText(split[0]) == "abcdef" && falcon::plainText(split[2]) == "mnop");
    const auto newline = falcon::wrapStyledText(measure, falcon::parseLegacyFormatting("one\ntwo"), 200, 5);
    CHECK(newline.size() == 2 && falcon::plainText(newline[1]) == "two");
    const auto coloured = falcon::wrapStyledText(measure, falcon::parseLegacyFormatting("\xC2\xA7" "caa bb \xC2\xA7" "9cc"), 200, 5);
    CHECK(coloured.size() == 1 && coloured[0].size() == 2 && coloured[0][1].rgb == 0x5555FF);  // styles survive wrapping
}
