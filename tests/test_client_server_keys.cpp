// Client tests: server, keys.
#include "test_support.hpp"

void test_server_list() {
    namespace fs = std::filesystem;
    const fs::path dir = fs::temp_directory_path() / "falcon_servers_test";
    std::error_code ignored;
    fs::remove_all(dir, ignored);

    falcon::ServerList missing(dir);
    missing.load();  // no file: an empty list, no error
    CHECK(missing.visible().empty() && missing.hiddenEntries().empty());

    falcon::ServerList list(dir);
    const auto make = [](const char* name, const char* ip) {
        auto e = std::make_shared<falcon::ServerEntry>();
        e->name = name;
        e->ip = ip;
        return e;
    };
    auto a = make("Alpha", "alpha.example.com");
    a->iconPng = {0x89, 'P', 'N', 'G', 1, 2, 3};
    a->packStatus = falcon::ServerPackStatus::Enabled;
    auto b = make("Beta \xC3\xA9", "beta.example.com:25570");
    b->packStatus = falcon::ServerPackStatus::Disabled;
    b->acceptedCodeOfConduct = -12345;
    list.add(a, false);
    list.add(b, false);
    list.add(make("Gamma", "gamma"), false);
    for (int i = 0; i < 20; ++i) list.add(make("Direct", ("direct" + std::to_string(i)).c_str()), true);
    CHECK(list.hiddenEntries().size() == falcon::ServerList::kMaxHidden);   // the history keeps the newest 16
    CHECK(list.hiddenEntries().front()->ip == "direct19");                // newest first
    CHECK(list.save());

    // The file is plain NBT (not gzip), the way the game reads it.
    const auto readAll = [&](const char* name) {
        std::ifstream in(dir / name, std::ios::binary);
        return std::vector<unsigned char>(std::istreambuf_iterator<char>(in), {});
    };
    const auto raw = readAll("servers.dat");
    CHECK(raw.size() > 4 && raw[0] == 0x0A && !(raw[0] == 0x1f && raw[1] == 0x8b));
    CHECK(!fs::exists(dir / "servers.dat.tmp"));

    falcon::ServerList loaded(dir);
    loaded.load();
    CHECK(loaded.visible().size() == 3 && loaded.hiddenEntries().size() == falcon::ServerList::kMaxHidden);
    if (loaded.visible().size() == 3) {
        CHECK(loaded.visible()[0]->name == "Alpha" && loaded.visible()[0]->ip == "alpha.example.com");
        CHECK(loaded.visible()[0]->iconPng == a->iconPng && loaded.visible()[0]->packStatus == falcon::ServerPackStatus::Enabled);
        CHECK(loaded.visible()[1]->name == "Beta \xC3\xA9" && loaded.visible()[1]->packStatus == falcon::ServerPackStatus::Disabled);
        CHECK(loaded.visible()[1]->acceptedCodeOfConduct == -12345);
        CHECK(loaded.visible()[2]->packStatus == falcon::ServerPackStatus::Prompt && loaded.visible()[2]->iconPng.empty());
    }
    CHECK(loaded.hiddenEntries().front()->ip == "direct19" && loaded.hiddenEntries().front()->hidden);

    // Reordering saves at once; lookups, unhide and remove behave like the game's.
    loaded.swap(0, 2);
    falcon::ServerList reread(dir);
    reread.load();
    CHECK(reread.visible().size() == 3 && reread.visible()[0]->name == "Gamma" && reread.visible()[2]->name == "Alpha");
    CHECK(fs::exists(dir / "servers.dat_old"));                      // the previous file is kept
    CHECK(reread.find("alpha.example.com") != nullptr && reread.find("direct10") != nullptr && reread.find("nope") == nullptr);
    const auto unhidden = reread.unhide("direct10");
    CHECK(unhidden != nullptr && !unhidden->hidden && reread.visible().size() == 4 && reread.visible().back() == unhidden);
    CHECK(reread.hiddenEntries().size() == falcon::ServerList::kMaxHidden - 1 && reread.unhide("direct10") == nullptr);
    reread.remove(reread.visible()[1]);
    CHECK(reread.visible().size() == 3);
    reread.swap(0, 99);  // out of range: nothing happens
    CHECK(reread.visible().size() == 3);

    // Damaged files never crash the reader.
    {
        std::ofstream out(dir / "servers.dat", std::ios::binary | std::ios::trunc);
        out << "this is not nbt";
    }
    falcon::ServerList damaged(dir);
    damaged.load();
    CHECK(damaged.visible().empty());
    fs::remove_all(dir, ignored);
}

#ifdef _WIN32
// A stand-in server on 127.0.0.1 that answers one status ping (and records what the client sent), so the
// protocol code can be tested without touching any real network.
class FakeStatusServer {
public:
    explicit FakeStatusServer(std::string json, bool answerStatus = true, bool answerPong = true)
        : json_(std::move(json)), answerStatus_(answerStatus), answerPong_(answerPong) {
        WSADATA data;
        WSAStartup(MAKEWORD(2, 2), &data);
        listener_ = socket(AF_INET, SOCK_STREAM, 0);
        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        bind(listener_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr));
        listen(listener_, 1);
        int length = sizeof(addr);
        getsockname(listener_, reinterpret_cast<sockaddr*>(&addr), &length);
        port = ntohs(addr.sin_port);
        thread_ = std::thread([this] { serve(); });
    }
    ~FakeStatusServer() {
        thread_.join();
        closesocket(listener_);
    }

    int port = 0;
    // What the client sent:
    int protocol = 0;
    std::string host;
    int hostPort = 0;
    int nextState = 0;
    bool gotStatusRequest = false;
    bool gotPingRequest = false;

private:
    bool readExact(SOCKET s, unsigned char* out, int n) {
        int got = 0;
        while (got < n) {
            const int r = recv(s, reinterpret_cast<char*>(out) + got, n - got, 0);
            if (r <= 0) return false;
            got += r;
        }
        return true;
    }
    bool readVarInt(SOCKET s, std::uint32_t& value) {
        value = 0;
        for (int i = 0; i < 5; ++i) {
            unsigned char b = 0;
            if (!readExact(s, &b, 1)) return false;
            value |= static_cast<std::uint32_t>(b & 0x7F) << (7 * i);
            if ((b & 0x80) == 0) return true;
        }
        return false;
    }
    static void putVarInt(std::vector<unsigned char>& out, std::uint32_t v) {
        do {
            unsigned char b = v & 0x7F;
            v >>= 7;
            if (v != 0) b |= 0x80;
            out.push_back(b);
        } while (v != 0);
    }
    bool readPacket(SOCKET s, std::uint32_t& id, std::vector<unsigned char>& rest) {
        std::uint32_t length = 0;
        if (!readVarInt(s, length) || length == 0 || length > 4096) return false;
        std::vector<unsigned char> body(length);
        if (!readExact(s, body.data(), static_cast<int>(length))) return false;
        std::size_t pos = 0;
        id = 0;
        for (int i = 0; i < 5 && pos < body.size(); ++i) {
            const unsigned char b = body[pos++];
            id |= static_cast<std::uint32_t>(b & 0x7F) << (7 * i);
            if ((b & 0x80) == 0) break;
        }
        rest.assign(body.begin() + static_cast<std::ptrdiff_t>(pos), body.end());
        return true;
    }
    void send(SOCKET s, const std::vector<unsigned char>& body) {
        std::vector<unsigned char> framed;
        putVarInt(framed, static_cast<std::uint32_t>(body.size()));
        framed.insert(framed.end(), body.begin(), body.end());
        ::send(s, reinterpret_cast<const char*>(framed.data()), static_cast<int>(framed.size()), 0);
    }

    void serve() {
        fd_set readable;
        FD_ZERO(&readable);
        FD_SET(listener_, &readable);
        timeval limit{5, 0};
        if (select(0, &readable, nullptr, nullptr, &limit) <= 0) return;  // nobody connected
        const SOCKET client = accept(listener_, nullptr, nullptr);
        if (client == INVALID_SOCKET) return;
        const DWORD ms = 5000;
        setsockopt(client, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&ms), sizeof(ms));

        std::uint32_t id = 0;
        std::vector<unsigned char> rest;
        if (readPacket(client, id, rest) && id == 0 && !rest.empty()) {  // the handshake
            std::size_t pos = 0;
            const auto varint = [&] {
                std::uint32_t v = 0;
                for (int i = 0; i < 5 && pos < rest.size(); ++i) {
                    const unsigned char b = rest[pos++];
                    v |= static_cast<std::uint32_t>(b & 0x7F) << (7 * i);
                    if ((b & 0x80) == 0) break;
                }
                return v;
            };
            protocol = static_cast<int>(varint());
            const std::uint32_t hostLength = varint();
            if (pos + hostLength + 3 <= rest.size()) {
                host.assign(reinterpret_cast<const char*>(rest.data()) + pos, hostLength);
                pos += hostLength;
                hostPort = (rest[pos] << 8) | rest[pos + 1];
                pos += 2;
                nextState = static_cast<int>(varint());
            }
        }
        if (readPacket(client, id, rest) && id == 0) gotStatusRequest = true;
        if (answerStatus_ && gotStatusRequest) {
            std::vector<unsigned char> body;
            putVarInt(body, 0);
            putVarInt(body, static_cast<std::uint32_t>(json_.size()));
            body.insert(body.end(), json_.begin(), json_.end());
            send(client, body);
            if (readPacket(client, id, rest) && id == 1 && rest.size() == 8) {
                gotPingRequest = true;
                if (answerPong_) {
                    std::vector<unsigned char> pong;
                    putVarInt(pong, 1);
                    pong.insert(pong.end(), rest.begin(), rest.end());
                    send(client, pong);
                }
            }
        }
        shutdown(client, SD_BOTH);
        closesocket(client);
    }

    std::string json_;
    bool answerStatus_;
    bool answerPong_;
    SOCKET listener_ = INVALID_SOCKET;
    std::thread thread_;
};
#endif

void test_server_ping() {
#ifdef _WIN32
    const std::string json =
        R"({"version":{"name":"26.3","protocol":777},"players":{"max":20,"online":3,"sample":[)"
        R"({"id":"069a79f4-44e9-4726-a5be-fca90e38aaf5","name":"Notch"},)"
        R"({"id":"00000000-0000-0000-0000-000000000000","name":"Anonymous Player"}]},)"
        R"("description":{"text":"Hi ","color":"gold","extra":[{"text":"there","bold":true}]},)"
        R"("favicon":"data:image/png;base64,AAEC"})";
    {
        FakeStatusServer server(json);
        const auto r = falcon::ServerPing::runBlocking("127.0.0.1", server.port);
        CHECK(r.state == falcon::PingState::Successful && r.message == falcon::PingMessage::None);
        CHECK(falcon::plainText(r.motd) == "Hi there" && r.motd.size() == 2 && r.motd[1].bold);
        CHECK(r.hasPlayers && r.playersOnline == 3 && r.playersMax == 20 && r.playersNotListed == 1);
        CHECK(r.playerSample.size() == 2 && r.playerSample[0] == "Notch" && r.playerSample[1].empty());  // "" = anonymous
        CHECK(r.versionName == "26.3" && !r.versionIsOld && r.protocol == 777);
        CHECK(r.faviconPng == std::vector<unsigned char>({0, 1, 2}));
        CHECK(r.pingMillis >= 0 && r.pingMillis < 2000);
        // The client spoke the status protocol and nothing else.
        CHECK(server.protocol == 777 && server.host == "127.0.0.1" && server.hostPort == server.port && server.nextState == 1);
        CHECK(server.gotStatusRequest && server.gotPingRequest);
    }
    {  // another protocol number is "incompatible", with the server's own version name
        FakeStatusServer server(R"({"version":{"name":"1.21.9","protocol":773},"players":{"max":5,"online":0},"description":"old"})");
        const auto r = falcon::ServerPing::runBlocking("127.0.0.1", server.port);
        CHECK(r.state == falcon::PingState::Incompatible && r.versionName == "1.21.9" && r.protocol == 773);
        CHECK(r.hasPlayers && r.playersNotListed == 0 && r.playerSample.empty() && falcon::plainText(r.motd) == "old");
    }
    {  // no version and no players at all: "Old", protocol 0, "???" for the player count
        FakeStatusServer server(R"({"description":"x"})");
        const auto r = falcon::ServerPing::runBlocking("127.0.0.1", server.port);
        CHECK(r.state == falcon::PingState::Incompatible && r.versionIsOld && r.protocol == 0 && !r.hasPlayers);
    }
    {  // a favicon that is not a PNG data URL is ignored
        FakeStatusServer server(R"({"version":{"name":"a","protocol":777},"favicon":"http://example.com/x.png"})");
        const auto r = falcon::ServerPing::runBlocking("127.0.0.1", server.port);
        CHECK(r.state == falcon::PingState::Successful && r.faviconPng.empty());
    }
    {  // a server that hangs up before answering, one that sends garbage, and one that never sends the pong
        FakeStatusServer silent("", /*answerStatus=*/false);
        const auto a = falcon::ServerPing::runBlocking("127.0.0.1", silent.port);
        CHECK(a.state == falcon::PingState::Unreachable && a.message == falcon::PingMessage::CannotConnect);
        FakeStatusServer garbage("this is not json");
        const auto b = falcon::ServerPing::runBlocking("127.0.0.1", garbage.port);
        CHECK(b.state == falcon::PingState::Unreachable && b.message == falcon::PingMessage::CannotConnect);
    }
    {  // nothing is listening: connection refused
        int freePort = 0;
        {
            FakeStatusServer probe("{}");
            freePort = probe.port;
            const auto r = falcon::ServerPing::runBlocking("127.0.0.1", probe.port);  // let it finish normally
            CHECK(r.state != falcon::PingState::Unreachable);
        }
        const auto r = falcon::ServerPing::runBlocking("127.0.0.1", freePort);
        CHECK(r.state == falcon::PingState::Unreachable && r.message == falcon::PingMessage::CannotConnect);
    }
    {  // the background version: start() returns at once and the snapshot fills in
        FakeStatusServer server(json);
        const auto ping = falcon::ServerPing::start("127.0.0.1:" + std::to_string(server.port));
        falcon::PingSnapshot s = ping->snapshot();
        for (int i = 0; i < 500 && s.state == falcon::PingState::Pinging; ++i) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            s = ping->snapshot();
        }
        CHECK(s.state == falcon::PingState::Successful && s.revision > 0);
        const auto bad = falcon::ServerPing::start("host:99999");  // not an address at all
        falcon::PingSnapshot t = bad->snapshot();
        for (int i = 0; i < 500 && t.state == falcon::PingState::Pinging; ++i) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            t = bad->snapshot();
        }
        CHECK(t.state == falcon::PingState::Unreachable && t.message == falcon::PingMessage::CannotResolve);
    }
    {  // cancel() before anything happened: the ping reports nothing and stays quiet
        const auto ping = falcon::ServerPing::start("127.0.0.1:1");
        ping->cancel();
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        CHECK(ping->snapshot().state == falcon::PingState::Pinging || ping->snapshot().state == falcon::PingState::Unreachable);
    }
#endif
}

void test_multiplayer_layout() {
    using falcon::Rect;
    // The spec's worked example: 1920x1080 at GUI scale 4 is 480x270.
    const auto l = falcon::computeMultiplayerLayout(480, 270, 100);
    CHECK((l.list == Rect{0, 33, 480, 177}));
    CHECK(l.titleX == 190 && l.titleY == 12);
    CHECK((l.join == Rect{86, 218, 100, 20}) && (l.direct == Rect{190, 218, 100, 20}) && (l.add == Rect{294, 218, 100, 20}));
    CHECK((l.edit == Rect{86, 242, 74, 20}) && (l.remove == Rect{164, 242, 74, 20}));
    CHECK((l.refresh == Rect{242, 242, 74, 20}) && (l.back == Rect{320, 242, 74, 20}));

    // A tick box toggles on a click and reports the new state; clicks outside it do nothing.
    falcon::CheckboxWidget box("Do not show this screen again", 10, 20, 100, false);
    bool state = false;
    int calls = 0;
    box.onChange = [&](bool checked) {
        state = checked;
        ++calls;
    };
    CHECK(box.width == 17 + 4 + 100 && box.height == 17);
    CHECK(!box.mouseClicked(5, 25, 0) && calls == 0);   // left of it
    CHECK(box.mouseClicked(12, 22, 0) && state && box.checked() && calls == 1);
    CHECK(box.mouseClicked(100, 25, 0) && !state && !box.checked() && calls == 2);  // on the label too
    CHECK(!box.mouseClicked(12, 22, 1) && calls == 2);   // only the left button
}

void test_key_mappings() {
    namespace fs = std::filesystem;
    using falcon::KeyBindings;
    using falcon::KeyId;
    const auto& mappings = falcon::keyMappings();

    // The registry: 62 actions in the game's eight categories, each name once.
    CHECK(mappings.size() == 62);
    std::array<int, 8> perCategory{};
    std::set<std::string> names;
    for (const auto& m : mappings) {
        ++perCategory[static_cast<std::size_t>(m.category)];
        names.insert(m.name);
    }
    CHECK(names.size() == 62);
    CHECK((perCategory == std::array<int, 8>{7, 8, 5, 3, 12, 2, 2, 23}));  // movement, misc, multiplayer, gameplay, ...

    // Key names in the options file are the game's.
    CHECK(KeyId::keyboard(GLFW_KEY_SPACE).optionName() == "key.keyboard.space");
    CHECK(KeyId::keyboard(GLFW_KEY_A).optionName() == "key.keyboard.a" && KeyId::keyboard(GLFW_KEY_5).optionName() == "key.keyboard.5");
    CHECK(KeyId::keyboard(GLFW_KEY_F5).optionName() == "key.keyboard.f5" && KeyId::keyboard(GLFW_KEY_KP_ENTER).optionName() == "key.keyboard.keypad.enter");
    CHECK(KeyId::keyboard(GLFW_KEY_LEFT_SUPER).optionName() == "key.keyboard.left.win");
    CHECK(KeyId::unbound().optionName() == "key.keyboard.unknown" && KeyId::unbound().isUnbound());
    CHECK(KeyId::mouse(0).optionName() == "key.mouse.left" && KeyId::mouse(1).optionName() == "key.mouse.right" &&
          KeyId::mouse(2).optionName() == "key.mouse.middle");
    CHECK(KeyId::mouse(3).optionName() == "key.mouse.4" && KeyId::mouse(7).optionName() == "key.mouse.8");  // the game counts from 1
    CHECK(!KeyId::mouse(0).isUnbound());
    // Every key GLFW can report survives a round trip through its name.
    int bindable = 0;
    for (int key = 0; key <= GLFW_KEY_LAST; ++key) {
        if (!KeyId::isBindableKey(key)) continue;
        ++bindable;
        const auto back = KeyId::fromOptionName(KeyId::keyboard(key).optionName());
        CHECK(back && *back == KeyId::keyboard(key));
    }
    CHECK(bindable > 100);
    for (int button = 0; button <= GLFW_MOUSE_BUTTON_LAST; ++button) {
        const auto back = KeyId::fromOptionName(KeyId::mouse(button).optionName());
        CHECK(back && *back == KeyId::mouse(button));
    }
    CHECK(KeyId::fromOptionName("key.keyboard.unknown") == KeyId::unbound());
    CHECK(!KeyId::fromOptionName("key.keyboard.nonsense") && !KeyId::fromOptionName("key.mouse.99") && !KeyId::fromOptionName("jump") &&
          !KeyId::fromOptionName("key.mouse.3") && !KeyId::fromOptionName("key.mouse.x"));
    CHECK(!KeyId::isBindableKey(-1) && !KeyId::isBindableKey(0));

    // Defaults, reset and "changed".
    KeyBindings bindings;
    CHECK(!bindings.anyChanged());
    std::size_t sneak = 0, right = 0, clearChat = 0, overlay = 0, modifier = 0, jump = 0, cinematic = 0, spectator = 0;
    for (std::size_t i = 0; i < mappings.size(); ++i) {
        const std::string n = mappings[i].name;
        if (n == "key.sneak") sneak = i;
        if (n == "key.right") right = i;
        if (n == "key.debug.clearChat") clearChat = i;
        if (n == "key.debug.overlay") overlay = i;
        if (n == "key.debug.modifier") modifier = i;
        if (n == "key.jump") jump = i;
        if (n == "key.smoothCamera") cinematic = i;
        if (n == "key.spectatorOutlines") spectator = i;
    }
    CHECK(bindings.key(jump) == KeyId::keyboard(GLFW_KEY_SPACE) && bindings.key(sneak) == KeyId::keyboard(GLFW_KEY_LEFT_SHIFT));
    CHECK(bindings.key(cinematic).isUnbound() && bindings.key(spectator).isUnbound());

    // Conflicts: two actions that both ship on one key (D, or F3) never flag each other; changing one does.
    CHECK(bindings.key(right) == bindings.key(clearChat) && bindings.conflicts(right).empty() && bindings.conflicts(clearChat).empty());
    CHECK(bindings.key(overlay) == bindings.key(modifier) && bindings.conflicts(overlay).empty());
    CHECK(bindings.conflicts(cinematic).empty() && bindings.conflicts(spectator).empty());  // unbound: no conflict
    bindings.set(sneak, KeyId::keyboard(GLFW_KEY_D));
    CHECK(!bindings.isDefault(sneak) && bindings.anyChanged());
    const auto sneakConflicts = bindings.conflicts(sneak);
    CHECK(sneakConflicts.size() == 2 && std::count(sneakConflicts.begin(), sneakConflicts.end(), right) == 1 &&
          std::count(sneakConflicts.begin(), sneakConflicts.end(), clearChat) == 1);
    const auto rightConflicts = bindings.conflicts(right);  // sneak is off its default, clear chat is not: only sneak
    CHECK(rightConflicts.size() == 1 && rightConflicts[0] == sneak);
    bindings.set(jump, KeyId::unbound());
    bindings.set(sneak, KeyId::unbound());
    CHECK(bindings.conflicts(jump).empty() && bindings.conflicts(sneak).empty());  // two unbound actions do not conflict
    bindings.reset(jump);
    CHECK(bindings.isDefault(jump) && bindings.anyChanged());
    bindings.resetAll();
    CHECK(!bindings.anyChanged() && bindings.key(sneak) == KeyId::keyboard(GLFW_KEY_LEFT_SHIFT));

    // Saving: key_<name>:<key name>; loading keeps the default for anything missing or unrecognised.
    bindings.set(jump, KeyId::mouse(3));
    bindings.set(sneak, KeyId::keyboard(GLFW_KEY_F7));
    falcon::OptionsFile file;
    bindings.save(file);
    CHECK(file.getString("key_key.jump") == "key.mouse.4" && file.getString("key_key.sneak") == "key.keyboard.f7");
    CHECK(file.getString("key_key.smoothCamera") == "key.keyboard.unknown" && file.getString("key_key.attack") == "key.mouse.left");
    KeyBindings loaded;
    loaded.load(falcon::OptionsFile::parse(file.toString()));
    CHECK(loaded.key(jump) == KeyId::mouse(3) && loaded.key(sneak) == KeyId::keyboard(GLFW_KEY_F7));
    CHECK(loaded.key(right) == KeyId::keyboard(GLFW_KEY_D));
    KeyBindings partial;
    partial.load(falcon::OptionsFile::parse("key_key.jump:key.keyboard.bogus\nkey_key.sneak:key.keyboard.z\nunrelated:1\n"));
    CHECK(partial.key(jump) == KeyId::keyboard(GLFW_KEY_SPACE));   // bad value: the default stays
    CHECK(partial.key(sneak) == KeyId::keyboard(GLFW_KEY_Z) && !partial.isDefault(sneak));

    // The Key Binds screen's order, from the game: by category, then the debug keys' own order, then by the
    // English names. This is the screen's list in the spec, id by id.
    struct Expected {
        const char* id;
        const char* label;
    };
    const std::vector<Expected> expected = {
        {"key.jump", "Jump"}, {"key.sneak", "Sneak"}, {"key.sprint", "Sprint"}, {"key.left", "Strafe Left"},
        {"key.right", "Strafe Right"}, {"key.back", "Walk Backward"}, {"key.forward", "Walk Forward"},
        {"key.advancements", "Advancements"}, {"key.quickActions", "Quick Actions"}, {"key.screenshot", "Take Screenshot"},
        {"key.smoothCamera", "Toggle Cinematic Camera"}, {"key.fullscreen", "Toggle Fullscreen"}, {"key.toggleGui", "Toggle GUI"},
        {"key.togglePerspective", "Toggle Perspective"}, {"key.toggleSpectatorShaderEffects", "Toggle Spectator Shader Effects"},
        {"key.friends", "Friends Screen"}, {"key.playerlist", "List Players"}, {"key.chat", "Open Chat"},
        {"key.command", "Open Command"}, {"key.socialInteractions", "Social Interactions Screen"},
        {"key.attack", "Attack/Destroy"}, {"key.pickItem", "Pick Block"}, {"key.use", "Use Item/Place Block"},
        {"key.drop", "Drop Selected Item"}, {"key.hotbar.1", "Hotbar Slot 1"}, {"key.hotbar.2", "Hotbar Slot 2"},
        {"key.hotbar.3", "Hotbar Slot 3"}, {"key.hotbar.4", "Hotbar Slot 4"}, {"key.hotbar.5", "Hotbar Slot 5"},
        {"key.hotbar.6", "Hotbar Slot 6"}, {"key.hotbar.7", "Hotbar Slot 7"}, {"key.hotbar.8", "Hotbar Slot 8"},
        {"key.hotbar.9", "Hotbar Slot 9"}, {"key.inventory", "Open/Close Inventory"}, {"key.swapOffhand", "Swap Item With Off Hand"},
        {"key.loadToolbarActivator", "Load Hotbar Activator"}, {"key.saveToolbarActivator", "Save Hotbar Activator"},
        {"key.spectatorOutlines", "Highlight Players"}, {"key.spectatorHotbar", "Select On Hotbar"},
        {"key.debug.overlay", "Toggle Overlay"}, {"key.debug.modifier", "Debug Modifier Key"}, {"key.debug.clearChat", "Clear Chat"},
        {"key.debug.copyRecreateCommand", "Copy Data"}, {"key.debug.copyLocation", "Copy Location"},
        {"key.debug.spectate", "Cycle Spectator"}, {"key.debug.crash", "Debug Crash"}, {"key.debug.debugOptions", "Debug Options"},
        {"key.debug.dumpDynamicTextures", "Dump Dynamic Textures"}, {"key.debug.dumpVersion", "Dump Version Info"},
        {"key.debug.switchGameMode", "Game Mode Switcher"}, {"key.debug.reloadChunk", "Reload Chunks"},
        {"key.debug.reloadResourcePacks", "Reload Resource Packs"}, {"key.debug.showAdvancedTooltips", "Show Advanced Tooltips"},
        {"key.debug.showChunkBorders", "Show Chunk Boundaries"}, {"key.debug.showHitboxes", "Show Hitboxes"},
        {"key.debug.profiling", "Start/Stop Profiling"}, {"key.debug.improvedTransparency", "Toggle Improved Transparency"},
        {"key.debug.focusPause", "Toggle Lost Focus Pause"}, {"key.debug.profilingChart", "Profiling Chart"},
        {"key.debug.fpsCharts", "Fps Charts"}, {"key.debug.networkCharts", "Network Charts"},
        {"key.debug.lightmapTexture", "Lightmap Texture"}};
    CHECK(expected.size() == 62);

    std::map<std::string, std::string> english;
    for (const auto& e : expected) english[e.id] = e.label;
    const auto order = falcon::sortedKeyMappingOrder([&](const std::string& key) { return english.at(key); });
    CHECK(order.size() == 62);
    bool sameOrder = order.size() == expected.size();
    for (std::size_t i = 0; sameOrder && i < order.size(); ++i) sameOrder = std::string(mappings[order[i]].name) == expected[i].id;
    CHECK(sameOrder);
    // Another language sorts by its own names.
    std::map<std::string, std::string> shuffled = english;
    shuffled["key.jump"] = "Zzz";
    const auto movement = falcon::sortedKeyMappingOrder([&](const std::string& key) { return shuffled.at(key); });
    CHECK(std::string(mappings[movement[6]].name) == "key.jump" && std::string(mappings[movement[0]].name) == "key.sneak");

    // Display names come from the language, with the printed letter for a letter key.
    const fs::path base = fs::temp_directory_path() / "falcon_keys_test";
    std::error_code ignored;
    fs::remove_all(base, ignored);
    writeText(base / "assets" / "minecraft" / "lang" / "en_us.json",
              R"({"key.keyboard.unknown":"Not Bound","key.keyboard.space":"Space","key.keyboard.left.shift":"Left Shift",)"
              R"("key.mouse":"Button %1$s","key.mouse.left":"Left Button","key.mouse.middle":"Middle Button"})");
    const falcon::ResourceManager resources(base / "assets");
    const falcon::Language lang = falcon::Language::load(resources, "en_us");
    CHECK(falcon::keyDisplayName(KeyId::unbound(), lang) == "Not Bound");
    CHECK(falcon::keyDisplayName(KeyId::keyboard(GLFW_KEY_SPACE), lang) == "Space" && falcon::keyDisplayName(KeyId::keyboard(GLFW_KEY_LEFT_SHIFT), lang) == "Left Shift");
    CHECK(falcon::keyDisplayName(KeyId::keyboard(GLFW_KEY_W), lang) == "W" && falcon::keyDisplayName(KeyId::keyboard(GLFW_KEY_7), lang) == "7");  // no layout name without GLFW
    CHECK(falcon::keyDisplayName(KeyId::mouse(0), lang) == "Left Button" && falcon::keyDisplayName(KeyId::mouse(2), lang) == "Middle Button");
    CHECK(falcon::keyDisplayName(KeyId::mouse(3), lang) == "Button 4" && falcon::keyDisplayName(KeyId::mouse(7), lang) == "Button 8");
    CHECK(falcon::layoutKeyName(GLFW_KEY_A).empty() && falcon::layoutKeyName(GLFW_KEY_SPACE).empty());  // GLFW is not running in the tests
    fs::remove_all(base, ignored);
}

void test_key_binds_layout() {
    using falcon::Rect;
    // The spec's worked example: 854x480 at GUI scale 2 is 427x240.
    const auto l = falcon::computeKeyBindsLayout(427, 240, 50);
    CHECK(l.titleX == 188 && l.titleY == 12);
    CHECK((l.list == Rect{0, 33, 427, 174}));
    CHECK((l.resetAll == Rect{59, 213, 150, 20}) && (l.done == Rect{217, 213, 150, 20}));
    // Inside a row: rowLeft = 427/2 - 170 = 43, name at 45, key button at 251, Reset at 331, the scrollbar at 391.
    const int rowLeft = 427 / 2 - falcon::kKeyBindsRowWidth / 2;
    CHECK(rowLeft == 43 && rowLeft + falcon::kKeyNameOffset == 45);
    CHECK(rowLeft + falcon::kKeyButtonOffset == 251 && rowLeft + falcon::kKeyButtonOffset + falcon::kKeyButtonWidth == 326);
    CHECK(rowLeft + falcon::kResetButtonOffset == 331 && rowLeft + falcon::kKeyBindsRowWidth + 8 == 391);

    // The text on a key button.
    using Piece = falcon::KeyLabelPiece;
    CHECK((falcon::keyButtonLabel("Space", false, false) == std::vector<Piece>{{"Space", 0xFFFFFF, false}}));
    CHECK((falcon::keyButtonLabel("D", true, false) == std::vector<Piece>{{"[", 0xFFFF55, false}, {"D", 0xFFFFFF, false}, {" ]", 0xFFFF55, false}}));
    CHECK((falcon::keyButtonLabel("K", false, true) == std::vector<Piece>{{">", 0xFFFF55, false}, {"K", 0xFFFFFF, true}, {" <", 0xFFFF55, false}}));
    const auto both = falcon::keyButtonLabel("D", true, true);  // waiting on a key that is already in use: "> [D ] <"
    CHECK(both.size() == 5 && both[0].text == ">" && both[1].text == "[" && both[1].underlined && both[2].underlined && both[4].text == " <" && !both[4].underlined);
}


falcon::Block blockNamed(const char* name) { return falcon::blockRegistry().find(name).value(); }

void test_block_registry() {
    // The built-in set used by the tests: names, properties, defaults.
    const auto minimal = falcon::BlockRegistry::makeMinimal();
    CHECK(!minimal->isFromGameData() && minimal->find("stone") && minimal->find("minecraft:stone") == minimal->find("stone"));
    CHECK(!minimal->find("no_such_block") && minimal->isAir(falcon::Block::Air) && !minimal->isAir(*minimal->find("stone")));
    const falcon::Block grass = *minimal->find("grass_block");
    CHECK(minimal->get(grass, "snowy") == "false");  // the default is the second state, like the game's
    const falcon::Block snowy = minimal->with(grass, "snowy", "true");
    CHECK(snowy != grass && minimal->get(snowy, "snowy") == "true" && minimal->with(snowy, "snowy", "false") == grass);
    CHECK(minimal->with(grass, "nope", "x") == grass && minimal->with(grass, "snowy", "maybe") == grass);
    CHECK(minimal->describe(snowy) == "grass_block[snowy=true]");
    const auto log = minimal->parse("minecraft:oak_log[axis=x]");
    CHECK(log && minimal->get(*log, "axis") == "x" && minimal->type(*log).name == "oak_log" && !minimal->parse("minecraft:nothing[a=b]"));
    CHECK(minimal->state(*minimal->find("stone")).has(falcon::kStateOpaqueCube) && minimal->state(*minimal->find("water")).has(falcon::kStateLiquid));
    // Ids are consecutive per block.
    const falcon::BlockType& oak = minimal->type(*log);
    CHECK(oak.stateCount == 3 && stateId(*log) >= oak.firstState && stateId(*log) < oak.firstState + oak.stateCount);

    // The real thing, when the game's data is on this machine: the numbers the game uses.
    const auto assets = falcon::locateAssets(std::nullopt);
    if (assets.location) {
        const std::filesystem::path data = assets.location->root.parent_path();
        const std::filesystem::path cache = std::filesystem::temp_directory_path() / "falcon_registry_test";
        std::error_code ignored;
        std::filesystem::remove_all(cache, ignored);
        std::string error;
        const auto real = falcon::BlockRegistry::loadFromGameData(data, cache, error);
        if (!real) {
            std::printf("  (block registry from game data skipped: %s)\n", error.c_str());
        } else {
            CHECK(real->isFromGameData() && real->stateCount() > 30000 && real->blocks().size() > 1000);
            const falcon::Block stone = *real->find("stone");
            CHECK(falcon::stateId(stone) == 1 && real->state(stone).has(falcon::kStateOpaqueCube) && std::abs(real->state(stone).hardness - 1.5f) < 1e-6f);
            CHECK(falcon::stateId(*real->find("grass_block")) == 9 && real->get(*real->find("grass_block"), "snowy") == "false");
            CHECK(falcon::stateId(*real->find("water")) == 89 && real->state(*real->find("water")).has(falcon::kStateWater));
            CHECK(real->state(*real->find("bedrock")).hardness < 0.0f);
            CHECK(std::abs(real->state(*real->find("ice")).friction - 0.98f) < 1e-6f && std::abs(real->state(*real->find("soul_sand")).speedFactor - 0.4f) < 1e-6f);
            CHECK(std::abs(real->state(*real->find("honey_block")).jumpFactor - 0.5f) < 1e-6f && real->state(*real->find("torch")).emission == 14);
            const auto stairs = real->parse("oak_stairs[facing=east,half=top]");
            CHECK(stairs && real->get(*stairs, "facing") == "east" && real->get(*stairs, "half") == "top" && real->get(*stairs, "shape") == "straight");
            const auto leaves = real->parse("oak_leaves[distance=3,persistent=true]");
            CHECK(leaves && real->get(*leaves, "distance") == "3" && real->get(*leaves, "persistent") == "true" && real->get(*leaves, "waterlogged") == "false");
            CHECK(real->state(*real->parse("oak_stairs[waterlogged=true]")).has(falcon::kStateWater) && real->state(*real->find("kelp")).has(falcon::kStateWater));
            // A second load comes from the cache and is the same.
            const auto again = falcon::BlockRegistry::loadFromGameData(data, cache, error);
            CHECK(again && again->stateCount() == real->stateCount() && again->blocks().size() == real->blocks().size() &&
                  again->describe(*stairs) == real->describe(*stairs) && again->state(stone).flags == real->state(stone).flags);
            std::filesystem::remove_all(cache, ignored);
        }
    }
}

void test_world_storage() {
    using falcon::Block;
    const falcon::CommonBlocks& cb = falcon::commonBlocks();
    // A section with one block type stores no array; editing it builds a new section and leaves the old one alone.
    const auto stone = falcon::uniformSection(cb.stone);
    CHECK(stone && stone->isUniform() && stone->at(3, 4, 5) == cb.stone && falcon::uniformSection(cb.stone) == stone);  // shared
    CHECK(falcon::uniformSection(Block::Air) == nullptr);
    const auto edited = falcon::sectionWithBlock(stone, 1, 2, 3, cb.dirt);
    CHECK(edited && !edited->isUniform() && edited->at(1, 2, 3) == cb.dirt && edited->at(0, 0, 0) == cb.stone);
    CHECK(edited->packing == falcon::Packing::Nibble && edited->palette.size() == 2);  // two kinds: 4 bits a block
    CHECK(stone->at(1, 2, 3) == cb.stone);                                           // the old section never changes
    const auto restored = falcon::sectionWithBlock(edited, 1, 2, 3, cb.stone);          // all the same again: compact again
    CHECK(restored && restored->isUniform() && restored->uniform == cb.stone);
    falcon::SectionBuilder air;
    CHECK(air.build() == nullptr);

    // Palettes: 17 kinds need 8 bits a block, more than 256 need the ids themselves; every block reads back.
    for (const int kinds : {16, 17, 255, 256, 257, 300}) {
        falcon::SectionBuilder b;
        for (int i = 0; i < falcon::kSectionVolume; ++i) b.raw()[static_cast<std::size_t>(i)] = static_cast<Block>(1 + (i * 7) % kinds);
        const auto s = b.build();
        const falcon::Packing want = kinds <= 16 ? falcon::Packing::Nibble : kinds <= 256 ? falcon::Packing::Byte : falcon::Packing::Wide;
        bool readsBack = s && s->packing == want;
        std::vector<Block> all(falcon::kSectionVolume);
        if (s) s->decode(all.data());
        for (int i = 0; i < falcon::kSectionVolume && readsBack; ++i) {
            readsBack = s->atIndex(i) == static_cast<Block>(1 + (i * 7) % kinds) && all[static_cast<std::size_t>(i)] == s->atIndex(i);
        }
        CHECK(readsBack);
        if (s) CHECK(s->memoryBytes() < (want == falcon::Packing::Nibble ? 2300u : want == falcon::Packing::Byte ? 4800u : 9000u));
    }
    falcon::SectionBuilder rowTest;
    for (int x = 0; x < 16; ++x) rowTest.set(x, 3, 4, static_cast<Block>(1 + x % 3));
    const auto row = rowTest.build();
    Block out[16];
    row->copyRow(2, 3, 4, 10, out);
    CHECK(out[0] == static_cast<Block>(1 + 2 % 3) && out[9] == static_cast<Block>(1 + 11 % 3));

    // A level: coordinates across chunk borders (negative ones too), air where nothing is loaded.
    falcon::Level level;
    auto chunk = std::make_shared<falcon::Chunk>();
    chunk->sections[0] = falcon::uniformSection(cb.stone);  // y -64..-49
    level.putChunk({-1, 2}, chunk);
    CHECK(level.block(-1, -64, 32) == cb.stone && level.block(-16, -49, 47) == cb.stone);
    CHECK(level.block(-17, -64, 32) == Block::Air && level.block(-1, -48, 32) == Block::Air);  // next chunk / next section
    CHECK(level.block(-1, falcon::kMinY - 1, 32) == Block::Air && level.block(-1, 400, 32) == Block::Air);
    CHECK(level.isLoaded(-5, 40) && !level.isLoaded(0, 0));
    const Block gravel = blockNamed("gravel");
    CHECK(level.setBlock(-1, -60, 32, gravel) && level.block(-1, -60, 32) == gravel);
    CHECK(!level.setBlock(0, -60, 0, gravel) && !level.setBlock(-1, 400, 32, gravel));
}

void test_terrain() {
    using falcon::Block;
    const falcon::CommonBlocks& cb = falcon::commonBlocks();
    // Superflat: bedrock, two dirt, grass at y -64..-61, air above.
    const falcon::TerrainGenerator flat(1, falcon::TerrainStyle::Flat);
    const auto fc = flat.generate(3, -4);
    CHECK(fc->at(5, -64, 5) == cb.bedrock && fc->at(5, -63, 5) == cb.dirt && fc->at(5, -62, 5) == cb.dirt);
    CHECK(fc->at(5, -61, 5) == cb.grassBlock && fc->at(5, -60, 5) == Block::Air && fc->at(0, 300, 15) == Block::Air);
    CHECK(flat.surfaceHeight(0, 0) == -61 && flat.findSpawn().y == -60.0);
    CHECK(fc->memoryBytes() < 12000);  // one section with an array; the rest is empty

    // Hills: the same seed gives the same chunk, another seed another one; rock below, soil on top, water in the sea.
    const falcon::TerrainGenerator hills(12345, falcon::TerrainStyle::Hills);
    const auto a = hills.generate(0, 0);
    const auto b = hills.generate(0, 0);
    bool same = true;
    for (int y = -64; y < 120 && same; y += 3) same = a->at(7, y, 9) == b->at(7, y, 9) && a->at(0, y, 0) == b->at(0, y, 0);
    CHECK(same);
    const falcon::TerrainGenerator other(54321, falcon::TerrainStyle::Hills);
    int differing = 0;
    for (int x = -300; x < 300; x += 20) differing += hills.surfaceHeight(x, 40) != other.surfaceHeight(x, 40);
    CHECK(differing > 10);
    for (const auto& [x, z] : std::vector<std::pair<int, int>>{{0, 0}, {37, -90}, {-250, 130}}) {
        const int h = hills.surfaceHeight(x, z);
        const auto chunk = hills.generate(falcon::chunkOf(x), falcon::chunkOf(z));
        const int lx = x & 15;
        const int lz = z & 15;
        CHECK(chunk->at(lx, -64, lz) == cb.bedrock && chunk->at(lx, h - 10, lz) == cb.stone);
        CHECK(chunk->at(lx, h, lz) == cb.grassBlock || chunk->at(lx, h, lz) == cb.sand);
        CHECK(chunk->at(lx, h + 1, lz) == (h + 1 < falcon::TerrainGenerator::kSeaLevel ? cb.water : Block::Air));
        CHECK(chunk->at(lx, std::max(h + 1, falcon::TerrainGenerator::kSeaLevel), lz) == Block::Air);
    }
    const auto spawn = hills.findSpawn();
    CHECK(hills.surfaceHeight(static_cast<int>(std::floor(spawn.x)), static_cast<int>(std::floor(spawn.z))) > falcon::TerrainGenerator::kSeaLevel);
    // Heights vary (it is not flat) and amplified terrain varies more.
    int lowest = 999, highest = -999, ampLowest = 999, ampHighest = -999;
    const falcon::TerrainGenerator amp(12345, falcon::TerrainStyle::Amplified);
    for (int x = -2000; x <= 2000; x += 40) {
        lowest = std::min(lowest, hills.surfaceHeight(x, 11));
        highest = std::max(highest, hills.surfaceHeight(x, 11));
        ampLowest = std::min(ampLowest, amp.surfaceHeight(x, 11));
        ampHighest = std::max(ampHighest, amp.surfaceHeight(x, 11));
    }
    CHECK(highest - lowest > 20 && ampHighest - ampLowest > highest - lowest);
}
