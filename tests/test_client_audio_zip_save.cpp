// Client tests: audio, zip, save.
#include "test_support.hpp"

void test_audio_mixer() {
    using falcon::AudioEngine;
    AudioEngine engine(AudioEngine::Mode::Offline, 48000);
    CHECK(engine.available() && engine.sampleRate() == 48000);
    std::vector<float> out(2 * 480);

    // A mono sound plays on both channels at its volume.
    engine.playSound(makeSound(48000, 1, 1000, 0.8f), 0.5f);
    engine.render(out.data(), 480);
    CHECK(near(out[0], 0.4f) && near(out[1], 0.4f) && near(out[2 * 479], 0.4f));
    CHECK(near(engine.takePeak(), 0.4f));
    CHECK(near(engine.takePeak(), 0.0f));  // the peak resets when read

    // 1000 frames at 48 kHz: still playing after 480, finished after 1000.
    engine.render(out.data(), 480);
    CHECK(near(out[2 * 479], 0.4f));
    engine.render(out.data(), 480);  // frames 960..1000 play, then silence
    CHECK(near(out[2 * 39], 0.4f) && near(out[2 * 41], 0.0f));
    engine.render(out.data(), 480);
    CHECK(near(out[0], 0.0f));

    // Two voices add up and the output is clipped to [-1, 1].
    engine.playSound(makeSound(48000, 1, 500, 0.9f), 1.0f);
    engine.playSound(makeSound(48000, 1, 500, 0.9f), 1.0f);
    engine.render(out.data(), 480);
    CHECK(near(out[0], 1.0f));
    engine.render(out.data(), 480);  // let the last 20 frames of both voices finish

    // Stereo sounds keep their channels; pitch 2 plays twice as fast.
    auto stereo = std::make_shared<falcon::PcmSound>();
    stereo->sampleRate = 48000;
    stereo->channels = 2;
    for (int i = 0; i < 400; ++i) {
        stereo->samples.push_back(0.2f);
        stereo->samples.push_back(-0.6f);
    }
    engine.playSound(stereo, 1.0f, 2.0f);
    engine.render(out.data(), 480);
    CHECK(near(out[0], 0.2f) && near(out[1], -0.6f));
    CHECK(near(out[2 * 150], 0.2f));  // 150 output frames = 300 source frames: still playing
    CHECK(near(out[2 * 250], 0.0f));  // 250 output frames > 200: finished
    CHECK(engine.stats().soundsStarted == 4);

    // A sample rate mismatch resamples: 300 frames at 24 kHz last 600 frames at 48 kHz.
    engine.playSound(makeSound(24000, 1, 300, 0.5f), 1.0f);
    engine.render(out.data(), 480);
    CHECK(near(out[2 * 479], 0.5f));  // output frame 479 is still inside the sound
    engine.render(out.data(), 480);
    CHECK(near(out[2 * 119], 0.5f) && near(out[2 * 121], 0.0f));  // ends at output frame 600
}

void test_sound_registry() {
    const auto registry = falcon::SoundRegistry::parse(R"({
      "ui.button.click": {"sounds": ["random/click_stereo"]},
      "music.menu": {"sounds": [
        {"name": "music/game/ebb", "stream": true, "volume": 0.4, "weight": 2},
        {"name": "music/menu/mutation", "stream": true}]},
      "redirect": {"sounds": [{"name": "ui.button.click", "type": "event", "volume": 0.5, "pitch": 2.0}]},
      "loop_a": {"sounds": [{"name": "loop_b", "type": "event"}]},
      "loop_b": {"sounds": [{"name": "loop_a", "type": "event"}]},
      "empty": {"sounds": []},
      "broken": 7
    })");
    CHECK(registry.size() == 6);  // "broken" is not an event
    std::mt19937 rng(1);

    const auto click = registry.pick("ui.button.click", rng);
    CHECK(click && click->name == "random/click_stereo" && click->volume == 1.0f && !click->stream);

    // Redirects follow the other event and multiply volume and pitch.
    const auto redirected = registry.pick("redirect", rng);
    CHECK(redirected && redirected->name == "random/click_stereo");
    CHECK(redirected && near(redirected->volume, 0.5f) && near(redirected->pitch, 2.0f));

    CHECK(!registry.pick("empty", rng).has_value());
    CHECK(!registry.pick("nope", rng).has_value());
    CHECK(!registry.pick("loop_a", rng).has_value());  // redirect cycles terminate

    // Weights: "ebb" (weight 2, volume 0.4) is picked about twice as often as "mutation".
    int ebb = 0;
    const int rolls = 3000;
    for (int i = 0; i < rolls; ++i) {
        const auto m = registry.pick("music.menu", rng);
        CHECK(m && m->stream);
        if (m && m->name == "music/game/ebb") {
            ++ebb;
            CHECK(near(m->volume, 0.4f));
        }
    }
    CHECK(ebb > rolls * 60 / 100 && ebb < rolls * 73 / 100);  // expected 66.7%
}

void test_sound_system_and_music() {
    namespace fs = std::filesystem;
    const fs::path empty = fs::temp_directory_path() / "falcon_empty_assets";
    fs::create_directories(empty);
    const falcon::ResourceManager resources(empty);
    falcon::AudioEngine engine(falcon::AudioEngine::Mode::Offline, 48000);
    falcon::GameOptions options;
    falcon::SoundSystem sounds(resources, engine, options, 1);

    // Volume math: clamp(instance) * clamp(category slider * master slider).
    options.setVolume(falcon::SoundSource::Master, 0.5);
    options.setVolume(falcon::SoundSource::Ui, 0.5);
    CHECK(near(sounds.calculateVolume(0.25f, falcon::SoundSource::Ui), 0.25f * 0.25f));
    CHECK(near(sounds.calculateVolume(1.0f, falcon::SoundSource::Master), 0.5f));  // master alone
    CHECK(near(sounds.calculateVolume(4.0f, falcon::SoundSource::Music), 0.5f));   // instance clamps to 1
    options.setVolume(falcon::SoundSource::Master, 0.0);
    CHECK(sounds.calculateVolume(1.0f, falcon::SoundSource::Music) == 0.0f);
    options.setVolume(falcon::SoundSource::Master, 1.0);
    options.setVolume(falcon::SoundSource::Ui, 1.0);

    // Preview sounds by category (master, music and voice have none).
    CHECK(std::string(falcon::SoundSystem::previewEvent(falcon::SoundSource::Weather)) ==
          "entity.lightning_bolt.thunder");
    CHECK(std::string(falcon::SoundSystem::previewEvent(falcon::SoundSource::Ui)) == "ui.button.click");
    CHECK(std::string(falcon::SoundSystem::previewEvent(falcon::SoundSource::Master)).empty());
    CHECK(std::string(falcon::SoundSystem::previewEvent(falcon::SoundSource::Voice)).empty());
    CHECK(std::string(falcon::soundSourceLangKey(falcon::SoundSource::Records)) == "soundCategory.record");

    // Music scheduling (with CONSTANT frequency so the gaps are deterministic). There are no
    // sounds in the empty asset folder, so every attempt fails - the schedule is what is tested.
    options.musicFrequency = falcon::MusicFrequency::Constant;
    falcon::MusicManager music(sounds, options, 7);
    for (int i = 0; i < 99; ++i) music.tick();
    CHECK(!music.hasCurrentTrack());      // still counting down from 100 ticks (5 s)
    music.tick();                         // tick 100: the first track is started
    CHECK(music.hasCurrentTrack());
    music.tick();                         // it never started, so the game schedules the next try
    CHECK(!music.hasCurrentTrack() && music.nextSongDelay() == 99);
    for (int i = 0; i < 98; ++i) music.tick();
    CHECK(!music.hasCurrentTrack());
    music.tick();                         // 100 ticks after the failure: tries again
    CHECK(music.hasCurrentTrack());

    // update() converts real time into 50 ms ticks and never catches up by more than a second.
    falcon::MusicManager music2(sounds, options, 9);
    music2.update(0.4f);
    music2.update(0.4f);
    CHECK(music2.nextSongDelay() == falcon::MusicManager::kStartingDelay);  // 0.8 ticks: none yet
    music2.update(0.4f);                                                   // 1.2 ticks: one tick
    CHECK(music2.nextSongDelay() == falcon::MusicManager::kStartingDelay - 1);
    music2.update(1000.0f);                                                // capped at 20 ticks
    CHECK(music2.nextSongDelay() == falcon::MusicManager::kStartingDelay - 21);

    // Default frequency: the gap after a track is a uniform 20..600 ticks.
    options.musicFrequency = falcon::MusicFrequency::Default;
    falcon::MusicManager music3(sounds, options, 11);
    int lo = 1 << 30;
    int hi = 0;
    for (int i = 0; i < 500; ++i) {
        const int d = music3.nextMenuDelay();
        lo = std::min(lo, d);
        hi = std::max(hi, d);
    }
    CHECK(lo >= 20 && hi <= 600 && lo < 100 && hi > 500);
    fs::remove_all(empty);
}

void test_now_playing_toast() {
    using falcon::NowPlayingToast;
    NowPlayingToast toast;
    CHECK(toast.visiblePortion(0.0) == 0.0 && !toast.active());
    toast.show("fingerspit - Ebb", 1000.0);
    CHECK(toast.active());
    CHECK(near(static_cast<float>(toast.visiblePortion(1000.0)), 0.0f));    // just started
    CHECK(near(static_cast<float>(toast.visiblePortion(1300.0)), 0.25f));   // halfway: eased (0.5^2)
    CHECK(near(static_cast<float>(toast.visiblePortion(1600.0)), 1.0f));    // slide-in done
    CHECK(near(static_cast<float>(toast.visiblePortion(6500.0)), 1.0f));    // 5 s hold
    CHECK(near(static_cast<float>(toast.visiblePortion(6900.0)), 0.75f));   // sliding out: 1 - 0.5^2
    CHECK(near(static_cast<float>(toast.visiblePortion(7300.0)), 0.0f));    // gone
    toast.hide();
    CHECK(toast.visiblePortion(3000.0) == 0.0);

    // The notes cycle white -> light grey -> ... : at tick 0 it is the (fixed) white.
    CHECK(NowPlayingToast::noteColor(0.0) == 0xFFE6E6E6u);
    CHECK(NowPlayingToast::noteColor(0.0) != NowPlayingToast::noteColor(15 * 25.0));
    CHECK(NowPlayingToast::noteColor(12 * 30 * 25.0) == NowPlayingToast::noteColor(0.0));  // 9 s cycle
}

// ---- helpers to build zips in memory ----------------------------------------------------------
void put16(std::vector<unsigned char>& b, unsigned v) {
    b.push_back(static_cast<unsigned char>(v & 0xFF));
    b.push_back(static_cast<unsigned char>((v >> 8) & 0xFF));
}
void put32(std::vector<unsigned char>& b, unsigned v) {
    put16(b, v & 0xFFFF);
    put16(b, v >> 16);
}


std::vector<unsigned char> makeZip(const std::vector<ZipFile>& files) {
    std::vector<unsigned char> out;
    std::vector<unsigned> offsets;
    std::vector<std::vector<unsigned char>> payloads;
    for (const ZipFile& f : files) {
        std::vector<unsigned char> payload;
        if (f.deflateStored) {
            payload.push_back(0x01);  // BFINAL=1, BTYPE=00 (stored)
            put16(payload, static_cast<unsigned>(f.data.size()));
            put16(payload, static_cast<unsigned>(~f.data.size()) & 0xFFFF);
        }
        payload.insert(payload.end(), f.data.begin(), f.data.end());
        payloads.push_back(payload);

        offsets.push_back(static_cast<unsigned>(out.size()));
        put32(out, 0x04034b50);
        put16(out, 20); put16(out, 0); put16(out, f.deflateStored ? 8 : 0);
        put16(out, 0); put16(out, 0); put32(out, 0);
        put32(out, static_cast<unsigned>(payload.size())); put32(out, static_cast<unsigned>(f.data.size()));
        put16(out, static_cast<unsigned>(f.name.size())); put16(out, 0);
        out.insert(out.end(), f.name.begin(), f.name.end());
        out.insert(out.end(), payload.begin(), payload.end());
    }
    const auto centralStart = static_cast<unsigned>(out.size());
    for (std::size_t i = 0; i < files.size(); ++i) {
        put32(out, 0x02014b50);
        put16(out, 20); put16(out, 20); put16(out, 0); put16(out, files[i].deflateStored ? 8 : 0);
        put16(out, 0); put16(out, 0); put32(out, 0);
        put32(out, static_cast<unsigned>(payloads[i].size())); put32(out, static_cast<unsigned>(files[i].data.size()));
        put16(out, static_cast<unsigned>(files[i].name.size())); put16(out, 0); put16(out, 0);
        put16(out, 0); put16(out, 0); put32(out, 0); put32(out, offsets[i]);
        out.insert(out.end(), files[i].name.begin(), files[i].name.end());
    }
    const auto centralSize = static_cast<unsigned>(out.size()) - centralStart;
    put32(out, 0x06054b50);
    put16(out, 0); put16(out, 0); put16(out, static_cast<unsigned>(files.size()));
    put16(out, static_cast<unsigned>(files.size())); put32(out, centralSize); put32(out, centralStart);
    put16(out, 0);
    return out;
}

void writeBytes(const std::filesystem::path& file, const std::vector<unsigned char>& bytes) {
    std::filesystem::create_directories(file.parent_path());
    std::ofstream out(file, std::ios::binary);
    out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
}

void test_zip_archive() {
    const auto zip = falcon::ZipArchive::fromBytes(makeZip({
        {"pack.mcmeta", R"({"pack":{"description":"hi"}})", false},
        {"assets/x.txt", "deflated payload", true},
    }));
    CHECK(zip.has_value());
    if (!zip) return;
    CHECK(zip->entryCount() == 2 && zip->contains("pack.mcmeta") && !zip->contains("nope"));
    CHECK(zip->readText("pack.mcmeta") == R"({"pack":{"description":"hi"}})");
    CHECK(zip->readText("assets/x.txt") == "deflated payload");  // method 8 goes through the inflater
    CHECK(!zip->read("missing").has_value());
    CHECK(!falcon::ZipArchive::fromBytes({1, 2, 3}).has_value());   // not a zip
}

void writeText(const std::filesystem::path& file, const std::string& text) {
    writeBytes(file, std::vector<unsigned char>(text.begin(), text.end()));
}

// --- tiny NBT writer for the tests (big-endian, like the real format) ---
void nbtBE(std::vector<unsigned char>& out, unsigned long long v, int bytes) {
    for (int i = bytes - 1; i >= 0; --i) out.push_back(static_cast<unsigned char>(v >> (8 * i)));
}
void nbtName(std::vector<unsigned char>& out, const std::string& s) {
    nbtBE(out, s.size(), 2);
    out.insert(out.end(), s.begin(), s.end());
}
void nbtHead(std::vector<unsigned char>& out, int type, const std::string& name) {
    out.push_back(static_cast<unsigned char>(type));
    nbtName(out, name);
}

// A gzip stream around `data` using one stored deflate block (the CRC is not checked by the reader).
std::vector<unsigned char> gzipStored(const std::vector<unsigned char>& data) {
    std::vector<unsigned char> out = {0x1f, 0x8b, 8, 0, 0, 0, 0, 0, 0, 0xff, 0x01};
    out.push_back(static_cast<unsigned char>(data.size() & 0xFF));
    out.push_back(static_cast<unsigned char>(data.size() >> 8));
    out.push_back(static_cast<unsigned char>(~data.size() & 0xFF));
    out.push_back(static_cast<unsigned char>((~data.size() >> 8) & 0xFF));
    out.insert(out.end(), data.begin(), data.end());
    for (int i = 0; i < 4; ++i) out.push_back(0);  // CRC32 (unchecked)
    for (int i = 0; i < 4; ++i) out.push_back(static_cast<unsigned char>(data.size() >> (8 * i)));
    return out;
}

void test_nbt() {
    std::vector<unsigned char> nbt;
    nbtHead(nbt, 10, "");                                          // root compound
    nbtHead(nbt, 10, "Data");
    nbtHead(nbt, 8, "LevelName"); nbtName(nbt, "My World");
    nbtHead(nbt, 3, "GameType"); nbtBE(nbt, 1, 4);
    nbtHead(nbt, 1, "hardcore"); nbtBE(nbt, 1, 1);
    nbtHead(nbt, 4, "LastPlayed"); nbtBE(nbt, 1700000000123ULL, 8);
    nbtHead(nbt, 5, "SpawnAngle"); nbtBE(nbt, 0x3FC00000u, 4);       // float 1.5
    nbtHead(nbt, 7, "Bytes"); nbtBE(nbt, 2, 4); nbt.push_back(0xFF); nbt.push_back(0x05);
    nbtHead(nbt, 9, "List"); nbt.push_back(3); nbtBE(nbt, 2, 4); nbtBE(nbt, 7, 4); nbtBE(nbt, 8, 4);
    nbtHead(nbt, 10, "Version");
    nbtHead(nbt, 8, "Name"); nbtName(nbt, "26.3");
    nbt.push_back(0);                                              // end Version
    nbt.push_back(0);                                              // end Data
    nbt.push_back(0);                                              // end root

    for (const auto& bytes : {nbt, gzipStored(nbt)}) {            // raw and gzip-compressed
        const auto root = falcon::parseNbt(bytes);
        CHECK(root.has_value());
        if (!root) continue;
        const falcon::NbtTag* data = root->get("Data");
        CHECK(data != nullptr);
        if (data == nullptr) continue;
        CHECK(data->getString("LevelName") == "My World" && data->getString("nope", "dflt") == "dflt");
        CHECK(data->getInt("GameType") == 1 && data->getBool("hardcore") && !data->getBool("missing"));
        CHECK(data->getInt("LastPlayed") == 1700000000123LL && data->getInt("absent", 42) == 42);
        CHECK(near(static_cast<float>(data->getDouble("SpawnAngle")), 1.5f));
        const falcon::NbtTag* bytesTag = data->get("Bytes");
        CHECK(bytesTag != nullptr && bytesTag->numbers.size() == 2 && bytesTag->numbers[0] == -1 && bytesTag->numbers[1] == 5);
        const falcon::NbtTag* list = data->get("List");
        CHECK(list != nullptr && list->items.size() == 2 && list->items[1].integer == 8);
        CHECK(data->get("Version") != nullptr && data->get("Version")->getString("Name") == "26.3");
        CHECK(data->getString("GameType", "x") == "x");           // wrong type falls back
    }

    // Corrupt input must fail cleanly: truncation, a huge list count, a bad gzip body.
    auto truncated = nbt;
    truncated.resize(truncated.size() - 6);
    CHECK(!falcon::parseNbt(truncated).has_value());
    std::vector<unsigned char> hugeList;
    nbtHead(hugeList, 10, ""); nbtHead(hugeList, 9, "L"); hugeList.push_back(3); nbtBE(hugeList, 0x7FFFFFFFu, 4);
    CHECK(!falcon::parseNbt(hugeList).has_value());
    auto badGzip = gzipStored(nbt);
    badGzip[badGzip.size() - 1] ^= 0x55;  // wrong ISIZE
    CHECK(!falcon::parseNbt(badGzip).has_value());
    CHECK(!falcon::parseNbt({}).has_value());
}

void test_options_file() {
    namespace fs = std::filesystem;
    falcon::OptionsFile f = falcon::OptionsFile::parse("fov:90\nname:a:b\r\nflag:true\nratio:0.25\nbroken line\n:nokey\nbad:12x\n");
    CHECK(f.getInt("fov", 70) == 90 && f.getInt("missing", 7) == 7 && f.getInt("bad", 5) == 5);
    CHECK(f.getString("name") == "a:b");                      // only the first ':' splits
    CHECK(f.getBool("flag", false) && !f.getBool("absent", false) && f.getBool("bad", true));
    CHECK(near(static_cast<float>(f.getDouble("ratio", 0.0)), 0.25f) && f.getDouble("bad", 1.5) == 1.5);

    f.set("fov", 100);
    f.set("music", 0.5);
    f.set("hc", true);
    const fs::path file = fs::temp_directory_path() / "falcon_options_test" / "o.txt";
    std::error_code ignored;
    fs::remove_all(file.parent_path(), ignored);
    CHECK(f.save(file));                                        // creates the folder, replaces atomically
    CHECK(f.save(file));                                        // saving again over an existing file works
    const falcon::OptionsFile back = falcon::OptionsFile::load(file);
    CHECK(back.getInt("fov", 0) == 100 && back.getBool("hc", false) && near(static_cast<float>(back.getDouble("music", 0)), 0.5f));
    CHECK(back.getString("name") == "a:b");                     // unknown/unused keys survive a round trip
    CHECK(!fs::exists(file.string() + ".tmp"));
    CHECK(!falcon::OptionsFile::load(file.parent_path() / "nope.txt").contains("fov"));
    fs::remove_all(file.parent_path(), ignored);
}

// Writes a world folder whose level.dat holds the given `Data` fields (gzip NBT) plus a session.lock.
void makeWorld(const std::filesystem::path& saves, const std::string& id, const std::vector<unsigned char>& dataFields,
               bool lock) {
    std::vector<unsigned char> nbt;
    nbtHead(nbt, 10, "");
    nbtHead(nbt, 10, "Data");
    nbt.insert(nbt.end(), dataFields.begin(), dataFields.end());
    nbt.push_back(0);  // end Data
    nbt.push_back(0);  // end root
    writeBytes(saves / id / "level.dat", gzipStored(nbt));
    if (lock) writeBytes(saves / id / "session.lock", {1, 2});
}

// The two examples from docs/UI.md, kept here so the documentation cannot go stale: if the library's
// API changes they stop compiling.
class GreetingScreen : public falcon::Screen {
public:
    using Screen::Screen;

protected:
    void init() override {
        addCenteredLabel(text("greeting.title"), width_ / 2, 40);
        auto row = falcon::layout::centeredRow(width_, 100, {falcon::metrics::kButtonWidth, falcon::metrics::kButtonWidth}, 8);
        addButton(text("gui.yes"), row[0], [this] { onClose(); });
        addButton(text("gui.no"), row[1], [this] { onClose(); });
        addLockedButton(text("greeting.locked"), falcon::layout::Column::centered(width_, 140, 200).next());
        addEditBox({falcon::layout::centeredX(width_, 200), 170, 200, 20}, text("greeting.hint"));
        addSlider({falcon::layout::centeredX(width_, 200), 200, 200, 20}, 0.5, [](double v) { return std::to_string(v); }, nullptr);
    }
};

class ExampleListScreen : public falcon::ListScreen {
public:
    explicit ExampleListScreen(falcon::ScreenNavigator& n) : ListScreen(n, "my.title") {}

protected:
    void addOptions() override {
        addHeader(text("my.section"));
        addPair(makeToggle("my.fast", fast_, kSmallWidth, [this](bool on) { fast_ = on; }),
                makeChoice("my.mode", {"my.mode.a", "my.mode.b"}, mode_, kSmallWidth, [this](int i) { mode_ = i; }));
        addBig(makeIntSlider(1, 10, level_, kBigWidth, [this](int v) { return optionLabel("my.level", std::to_string(v)); },
                             [this](int v) { level_ = v; }));
        addPair(withTooltip(makeSlider(0.5, kSmallWidth, [this](double v) { return percentLabel("my.opacity", v); }, nullptr),
                            "my.opacity.tooltip"),
                makeLink("my.more", kSmallWidth, [] { return std::unique_ptr<falcon::Screen>(); }));
    }
    bool fast_ = false;
    int mode_ = 0;
    int level_ = 5;
};
static_assert(!std::is_abstract_v<GreetingScreen> && !std::is_abstract_v<ExampleListScreen>,
              "the documented example screens must be complete");

void test_ui_library_layout() {
    using falcon::Rect;
    namespace layout = falcon::layout;
    // Everything is positioned with truncating integer math, like the game's layout code.
    CHECK(layout::centeredX(427, 200) == 113 && layout::centeredY(240, 21) == 109);
    const auto row = layout::centeredRow(427, 100, {150, 150}, 8);
    CHECK(row.size() == 2 && (row[0] == Rect{59, 100, 150, 20}) && (row[1] == Rect{217, 100, 150, 20}));
    const auto footer = layout::footerRow(427, 240, 2, 150);  // Accessibility's Guide | Done
    CHECK((footer[0] == Rect{59, 213, 150, 20}) && (footer[1] == Rect{217, 213, 150, 20}));
    CHECK((layout::gridCell(427, 37, 0, 0) == Rect{58, 37, 150, 20}) && (layout::gridCell(427, 37, 1, 2) == Rect{218, 87, 150, 20}));

    auto column = layout::Column::centered(427, 108, 200, 20, 4);  // like the title screen's buttons
    CHECK((column.next() == Rect{113, 108, 200, 20}) && (column.next() == Rect{113, 132, 200, 20}));
    column.skip(12);
    CHECK((column.next() == Rect{113, 168, 200, 20}));

    // The list screen shell: title at y=12, list between the 33px strips, Done at H-27.
    const auto l = falcon::computeListScreenLayout(427, 240, 100);
    CHECK((l.title == Rect{163, 12, 100, 9}) && (l.list == Rect{0, 33, 427, 174}) && (l.done == Rect{113, 213, 200, 20}));
}

void test_languages() {
    namespace fs = std::filesystem;
    const fs::path base = fs::temp_directory_path() / "falcon_lang_test";
    std::error_code ignored;
    fs::remove_all(base, ignored);
    const fs::path assets = base / "assets";
    writeText(assets / "minecraft" / "lang" / "en_us.json", R"({"a":"Alpha","b":"Beta","c":"Gamma"})");
    writeText(assets / "minecraft" / "lang" / "de_de.json", R"j({"a":"Alfa","c":"Gamma (de)"})j");
    writeText(assets / "minecraft" / "lang" / "empty.json", "not json");
    writeText(assets / "pack.mcmeta",
              R"({"language":{"fr_fr":{"name":"Francais","region":"France","bidirectional":false},)"
              R"("de_de":{"name":"Deutsch","region":"Deutschland","bidirectional":false},)"
              R"("ar_sa":{"name":"Arabic","region":"Saudi","bidirectional":true}}})");

    const falcon::ResourceManager resources(assets);
    const falcon::Language english = falcon::Language::load(resources, "en_us");
    CHECK(english.get("a") == "Alpha" && english.get("missing") == "missing");
    // Another language goes on top of English: its lines win, the rest stays English.
    const falcon::Language german = falcon::Language::load(resources, "de_de");
    CHECK(german.get("a") == "Alfa" && german.get("b") == "Beta" && german.get("c") == "Gamma (de)");
    bool threw = false;
    try { (void)falcon::Language::load(resources, "xx_xx"); } catch (const std::exception&) { threw = true; }
    CHECK(threw);  // a language with no file is an error the caller can handle
    threw = false;
    try { (void)falcon::Language::load(resources, "empty"); } catch (const std::exception&) { threw = true; }
    CHECK(threw);

    // The list is ordered by code, not by name, and carries name, region and direction.
    const auto list = falcon::loadLanguageList(resources);
    CHECK(list.size() == 3 && list[0].code == "ar_sa" && list[1].code == "de_de" && list[2].code == "fr_fr");
    CHECK(list[1].display() == "Deutsch (Deutschland)" && list[0].rtl && !list[1].rtl);
    fs::remove_all(assets / "pack.mcmeta", ignored);
    const auto fallback = falcon::loadLanguageList(resources);  // no list: English only, like the game
    CHECK(fallback.size() == 1 && fallback[0].code == "en_us" && fallback[0].display() == "English (US)");

    falcon::LanguageInfo korean{"ko_kr", "Korean", "South Korea", false};
    CHECK(falcon::languageMatchesFilter(korean, "") && falcon::languageMatchesFilter(korean, "KOR") &&
          falcon::languageMatchesFilter(korean, "south") && !falcon::languageMatchesFilter(korean, "japan"));
    fs::remove_all(base, ignored);
}

// The Graphics API setting: OpenGL or Vulkan (no "Default"), kept by name, with the old numeric option migrated.
void test_graphics_api_setting() {
    using falcon::GraphicsApi;
    CHECK(falcon::graphicsApiId(GraphicsApi::OpenGL) == "opengl" && falcon::graphicsApiId(GraphicsApi::Vulkan) == "vulkan");
    CHECK(falcon::parseGraphicsApi("vulkan") == GraphicsApi::Vulkan && falcon::parseGraphicsApi("opengl", GraphicsApi::Vulkan) == GraphicsApi::OpenGL);
    CHECK(falcon::parseGraphicsApi("default") == GraphicsApi::OpenGL && falcon::parseGraphicsApi("nonsense", GraphicsApi::Vulkan) == GraphicsApi::Vulkan);  // not a choice any more
    CHECK(falcon::graphicsApiTranslationKey(GraphicsApi::OpenGL) == "options.graphicsApi.opengl" && falcon::graphicsApiTranslationKey(GraphicsApi::Vulkan) == "options.graphicsApi.vulkan");
    CHECK(falcon::kGraphicsApiCount == 2);
    // Only OpenGL can draw the game today: asking for Vulkan is remembered but OpenGL is what runs, and the reason is said.
    std::string note = "stale";
    CHECK(falcon::graphicsApiRenders(GraphicsApi::OpenGL) && !falcon::graphicsApiRenders(GraphicsApi::Vulkan));
    CHECK(falcon::effectiveGraphicsApi(GraphicsApi::OpenGL, &note) == GraphicsApi::OpenGL && note.empty());
    CHECK(falcon::effectiveGraphicsApi(GraphicsApi::Vulkan, &note) == GraphicsApi::OpenGL && note.find("vulkan") != std::string::npos);
    CHECK(falcon::effectiveGraphicsApi(GraphicsApi::Vulkan) == GraphicsApi::OpenGL);  // (also without a note to fill)
    CHECK(falcon::graphicsApiTooltip(GraphicsApi::Vulkan).find("OpenGL") != std::string::npos && falcon::graphicsApiTooltip(GraphicsApi::OpenGL).find("Not available") == std::string::npos);

    // The option: OpenGL by default, saved by name, read back.
    falcon::GameOptions defaults;
    CHECK(defaults.graphicsApi == GraphicsApi::OpenGL);
    falcon::GameOptions chosen;
    chosen.graphicsApi = GraphicsApi::Vulkan;
    falcon::OptionsFile file;
    chosen.save(file);
    CHECK(file.getString("graphicsBackend") == "vulkan");
    falcon::GameOptions back;
    back.load(falcon::OptionsFile::parse(file.toString()));
    CHECK(back.graphicsApi == GraphicsApi::Vulkan);
    // An options file from before: the number had a "Default" (0), OpenGL (1) and Vulkan (2). Only Vulkan stays a different choice.
    for (const auto& [number, expected] : {std::pair<int, GraphicsApi>{0, GraphicsApi::OpenGL}, {1, GraphicsApi::OpenGL}, {2, GraphicsApi::Vulkan}}) {
        falcon::GameOptions old;
        old.load(falcon::OptionsFile::parse("graphicsApi:" + std::to_string(number) + "\n"));
        CHECK(old.graphicsApi == expected);
    }
    // The new key wins over a stale old one.
    falcon::GameOptions both;
    both.load(falcon::OptionsFile::parse("graphicsApi:2\ngraphicsBackend:opengl\n"));
    CHECK(both.graphicsApi == GraphicsApi::OpenGL);
}

// The game mode switcher (F3 + F4): the game's slot order and geometry, F4 cycling and the mode that is picked when it opens.
void test_gamemode_switcher_logic() {
    using falcon::GameModeSwitcherScreen;
    // GameModeIcon order: Creative, Survival, Adventure, Spectator; the game types are 0 survival, 1 creative, 2 adventure, 3 spectator.
    CHECK(GameModeSwitcherScreen::gameTypeOfSlot(0) == 1 && GameModeSwitcherScreen::gameTypeOfSlot(1) == 0 && GameModeSwitcherScreen::gameTypeOfSlot(2) == 2 && GameModeSwitcherScreen::gameTypeOfSlot(3) == 3);
    for (int type = 0; type < 4; ++type) CHECK(GameModeSwitcherScreen::gameTypeOfSlot(GameModeSwitcherScreen::slotOfGameType(type)) == type);
    // F4 goes round: Creative -> Survival -> Adventure -> Spectator -> Creative.
    CHECK(GameModeSwitcherScreen::nextMode(0) == 1 && GameModeSwitcherScreen::nextMode(1) == 2 && GameModeSwitcherScreen::nextMode(2) == 3 && GameModeSwitcherScreen::nextMode(3) == 0);
    // What is picked when it opens: the mode before this one if there was one, else Survival from Creative and Creative from the rest.
    CHECK(GameModeSwitcherScreen::defaultSlot(/*current*/ 1, /*previous*/ -1) == GameModeSwitcherScreen::slotOfGameType(0));
    CHECK(GameModeSwitcherScreen::defaultSlot(0, -1) == GameModeSwitcherScreen::slotOfGameType(1));
    CHECK(GameModeSwitcherScreen::defaultSlot(3, -1) == GameModeSwitcherScreen::slotOfGameType(1) && GameModeSwitcherScreen::defaultSlot(2, -1) == GameModeSwitcherScreen::slotOfGameType(1));
    CHECK(GameModeSwitcherScreen::defaultSlot(1, 3) == GameModeSwitcherScreen::slotOfGameType(3) && GameModeSwitcherScreen::defaultSlot(0, 2) == GameModeSwitcherScreen::slotOfGameType(2));
    // Geometry: four 26 x 26 slots 31 apart (119 wide), centred, their top 31 above the middle of the screen.
    CHECK(GameModeSwitcherScreen::kAllSlotsWidth == 119 && GameModeSwitcherScreen::kSlotArea == 26 && GameModeSwitcherScreen::kSlotAreaPadded == 31);
    CHECK(GameModeSwitcherScreen::slotLeft(427, 0) == 427 / 2 - 59 && GameModeSwitcherScreen::slotLeft(427, 3) == 427 / 2 - 59 + 93);
    CHECK(GameModeSwitcherScreen::slotTop(240) == 120 - 31);
}

// The compressed chunk cache: a chunk that goes in comes back with the same blocks and biomes, in much fewer bytes.
void test_chunk_cache() {
    using namespace falcon;
    const auto sameBlocks = [](const Chunk& a, const Chunk& b) {
        for (int y = kMinY; y < kMaxY; y += 3) {
            for (int z = 0; z < 16; z += 3) {
                for (int x = 0; x < 16; x += 3) {
                    if (a.at(x, y, z) != b.at(x, y, z)) return false;
                }
            }
        }
        return true;
    };
    auto chunk = std::make_shared<Chunk>();
    // A solid section, a nibble-packed one (up to 16 kinds), a byte-packed one (up to 256) and a wide one (more kinds than that).
    {
        SectionBuilder solid;
        solid.fill(static_cast<Block>(7));
        chunk->sections[0] = solid.build();
        SectionBuilder few;
        for (int i = 0; i < 4096; ++i) few.set(i & 15, i >> 8, (i >> 4) & 15, static_cast<Block>(1 + (i * 7) % 5));
        chunk->sections[3] = few.build();
        SectionBuilder some;
        for (int i = 0; i < 4096; ++i) some.set(i & 15, i >> 8, (i >> 4) & 15, static_cast<Block>(1 + (i * 13) % 60));
        chunk->sections[5] = some.build();
        SectionBuilder many;
        for (int i = 0; i < 4096; ++i) many.set(i & 15, i >> 8, (i >> 4) & 15, static_cast<Block>(1 + (i * 37) % 700));
        chunk->sections[9] = many.build();
        // (the rest stays air)
    }
    auto grid = std::make_shared<BiomeGrid>();
    for (std::size_t i = 0; i < grid->ids.size(); ++i) grid->ids[i] = static_cast<std::uint8_t>(i % 11);
    chunk->biomes = grid;

    const std::vector<unsigned char> packed = ChunkCache::compress(*chunk);
    CHECK(!packed.empty());
    const auto back = ChunkCache::decompress(packed);
    CHECK(back != nullptr);
    if (!back) return;
    CHECK(sameBlocks(*chunk, *back) && back->sections[1] == nullptr && back->sections[0] != nullptr && back->sections[9] != nullptr);
    CHECK(back->biomes != nullptr && back->biomes->ids == grid->ids);
    CHECK(packed.size() < chunk->memoryBytes());  // smaller than the chunk as it sits in memory
    CHECK(ChunkCache::decompress({1, 2, 3}) == nullptr && ChunkCache::decompress({}) == nullptr);  // garbage is refused, not trusted

    // The cache: hold, pack (what a worker does), take back; forgotten once taken.
    ChunkCache cache(1u << 20);
    const ChunkPos here{4, -2};
    CHECK(!cache.contains(here) && cache.take(here) == nullptr);
    cache.hold(here, chunk);
    CHECK(cache.contains(here) && cache.count() == 1 && cache.packedBytes() == 0);  // not packed yet
    const auto early = cache.take(here);  // taken before it was packed: the chunk itself, minus its light
    CHECK(early != nullptr && sameBlocks(*chunk, *early) && !cache.contains(here));
    cache.hold(here, chunk);
    cache.pack(here);
    CHECK(cache.contains(here) && cache.packedBytes() > 0 && cache.packedBytes() < 40000);
    const auto restored = cache.take(here);
    CHECK(restored != nullptr && sameBlocks(*chunk, *restored) && !cache.contains(here) && cache.packedBytes() == 0);

    // Over the budget the chunks furthest from the player are forgotten first.
    ChunkCache small(packed.size() * 3 + packed.size() / 2);
    for (int i = 0; i < 6; ++i) {
        small.hold({i * 10, 0}, chunk);
        small.pack({i * 10, 0});
    }
    CHECK(small.count() == 6);
    small.trim({0, 0});
    CHECK(small.count() == 3 && small.contains({0, 0}) && small.contains({10, 0}) && small.contains({20, 0}) && !small.contains({50, 0}));
}
