// The commands of the game that Falcon can run: the typed text is parsed with the game's own command tree (command_tree.hpp), then the commands that mean something
// in a world without a server are carried out. The ones that need what Falcon does not have (scoreboards, advancements, a day, weather, mobs ...) say so.
#include "falcon/game/command_tree.hpp"
#include "falcon/game/game_session.hpp"
#include "falcon/game/item_names.hpp"
#include "falcon/text/styled_text.hpp"
#include "falcon/util/text_util.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <map>
#include <set>

namespace falcon {

namespace {

constexpr const char* kSection = "\xC2\xA7";

std::string lowerCase(std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

std::string stripNamespace(std::string s) {
    if (s.rfind("minecraft:", 0) == 0) s.erase(0, 10);
    return s;
}

// "Gave %s %s to %s" with its arguments: %s (and %1$s ...) in order.
std::string fill(std::string pattern, const std::vector<std::string>& args) {
    std::string out;
    std::size_t next = 0;
    for (std::size_t i = 0; i < pattern.size(); ++i) {
        if (pattern[i] != '%' || i + 1 >= pattern.size()) {
            out += pattern[i];
            continue;
        }
        if (pattern[i + 1] == '%') {
            out += '%';
            ++i;
        } else if (pattern[i + 1] == 's') {
            if (next < args.size()) out += args[next++];
            ++i;
        } else if (std::isdigit(static_cast<unsigned char>(pattern[i + 1])) && i + 3 < pattern.size() && pattern[i + 2] == '$' && pattern[i + 3] == 's') {
            const std::size_t n = static_cast<std::size_t>(pattern[i + 1] - '1');
            if (n < args.size()) out += args[n];
            i += 3;
        } else {
            out += pattern[i];
        }
    }
    return out;
}

std::string decimals(double v) {
    char buf[64];
    std::snprintf(buf, sizeof buf, "%f", v);
    return buf;
}

// The game rules the game has, with the values a new world starts with.
const std::map<std::string, std::string>& gameRuleDefaults() {
    static const std::map<std::string, std::string> rules = {
        {"advance_time", "true"}, {"advance_weather", "true"}, {"allow_entering_nether_using_portals", "true"}, {"block_drops", "true"}, {"block_explosion_drop_decay", "true"},
        {"command_block_output", "true"}, {"command_blocks_work", "true"}, {"drowning_damage", "true"}, {"elytra_movement_check", "true"}, {"ender_pearls_vanish_on_death", "true"},
        {"entity_drops", "true"}, {"fall_damage", "true"}, {"fire_damage", "true"}, {"fire_spread_radius_around_player", "128"}, {"forgive_dead_players", "true"},
        {"freeze_damage", "true"}, {"global_sound_events", "true"}, {"immediate_respawn", "false"}, {"keep_inventory", "false"}, {"lava_source_conversion", "false"},
        {"limited_crafting", "false"}, {"locator_bar", "true"}, {"log_admin_commands", "true"}, {"max_block_modifications", "32768"}, {"max_command_forks", "65536"},
        {"max_command_sequence_length", "65536"}, {"max_entity_cramming", "24"}, {"max_minecart_speed", "8"}, {"max_snow_accumulation_height", "1"}, {"mob_drops", "true"},
        {"mob_explosion_drop_decay", "true"}, {"mob_griefing", "true"}, {"natural_health_regeneration", "true"}, {"player_movement_check", "true"},
        {"players_nether_portal_creative_delay", "0"}, {"players_nether_portal_default_delay", "80"}, {"players_sleeping_percentage", "100"}, {"projectiles_can_break_blocks", "true"},
        {"pvp", "true"}, {"raids", "true"}, {"random_tick_speed", "3"}, {"reduced_debug_info", "false"}, {"respawn_radius", "10"}, {"send_command_feedback", "true"},
        {"show_advancement_messages", "true"}, {"show_death_messages", "true"}, {"spawn_mobs", "true"}, {"spawn_monsters", "true"}, {"spawn_patrols", "true"},
        {"spawn_phantoms", "true"}, {"spawn_wandering_traders", "true"}, {"spawn_wardens", "true"}, {"spawner_blocks_work", "true"}, {"spectators_generate_chunks", "true"},
        {"spread_vines", "true"}, {"tnt_explodes", "true"}, {"tnt_explosion_drop_decay", "false"}, {"universal_anger", "false"}, {"water_source_conversion", "true"}};
    return rules;
}

}  // namespace

// ---------------------------------------------------------------------------------------------------------------

struct CommandRunner {
    GameSession& s;
    std::vector<std::string> literals;                       // the words typed as they are
    std::vector<std::pair<std::string, std::string>> args;   // argument node name -> its text
    std::string command;                                     // "give"

    // ---- output ----
    void say(const std::string& text) { s.addChatMessage(text); }
    void fail(const std::string& text) { s.addChatMessage(std::string(kSection) + "c" + text); }
    [[nodiscard]] std::string tr(const std::string& key, const std::vector<std::string>& values = {}) const { return fill(s.services_.language.get(key), values); }
    void unsupported(const std::string& what) { fail("Falcon cannot run " + what + " yet: it needs " + std::string(what == "/time" || what == "/weather" ? "a day and weather" : "something Falcon does not have")); }

    // ---- arguments ----
    [[nodiscard]] const std::string* find(const std::string& name) const {
        for (const auto& a : args) {
            if (a.first == name) return &a.second;
        }
        return nullptr;
    }
    [[nodiscard]] bool has(const std::string& literal) const { return std::find(literals.begin(), literals.end(), literal) != literals.end(); }
    [[nodiscard]] std::string arg(const std::string& name, const std::string& fallback = "") const {
        const std::string* v = find(name);
        return v != nullptr ? *v : fallback;
    }

    // ---- selectors ----
    struct Selector {
        char kind = 'N';  // s p a r e n, or N for a name
        std::string name;
        std::map<std::string, std::string> options;
        std::string error;
    };
    [[nodiscard]] Selector selector(const std::string& text) const {
        Selector sel;
        if (text.empty() || text[0] != '@') {
            sel.name = text;
            return sel;
        }
        sel.kind = text.size() > 1 ? text[1] : 's';
        if (text.size() > 3 && text[2] == '[') {
            const std::string inner = text.substr(3, text.size() - 4);
            std::size_t start = 0;
            int depth = 0;
            for (std::size_t i = 0; i <= inner.size(); ++i) {
                if (i < inner.size() && (inner[i] == '[' || inner[i] == '{')) ++depth;
                if (i < inner.size() && (inner[i] == ']' || inner[i] == '}')) --depth;
                if (i == inner.size() || (inner[i] == ',' && depth == 0)) {
                    std::string pair = inner.substr(start, i - start);
                    start = i + 1;
                    while (!pair.empty() && pair.front() == ' ') pair.erase(0, 1);
                    while (!pair.empty() && pair.back() == ' ') pair.pop_back();
                    if (pair.empty()) continue;
                    const std::size_t eq = pair.find('=');
                    if (eq == std::string::npos) {
                        sel.error = "Expected value for option '" + pair + "'";
                        continue;
                    }
                    sel.options[pair.substr(0, eq)] = pair.substr(eq + 1);
                }
            }
        }
        return sel;
    }
    // type=... accepts a name or a !name; true if the kind of entity passes.
    [[nodiscard]] static bool typePasses(const Selector& sel, const std::string& type) {
        const auto it = sel.options.find("type");
        if (it == sel.options.end()) return true;
        std::string wanted = it->second;
        bool negate = false;
        if (!wanted.empty() && wanted[0] == '!') {
            negate = true;
            wanted.erase(0, 1);
        }
        return (stripNamespace(wanted) == type) != negate;
    }
    // distance=..5, 5.., 2..5, 3: of the player's feet; true if the point passes.
    [[nodiscard]] bool distancePasses(const Selector& sel, double x, double y, double z) const {
        const auto it = sel.options.find("distance");
        if (it == sel.options.end()) return true;
        const double d = std::sqrt((x - s.player_.x) * (x - s.player_.x) + (y - s.player_.y) * (y - s.player_.y) + (z - s.player_.z) * (z - s.player_.z));
        const std::string& r = it->second;
        const std::size_t dots = r.find("..");
        if (dots == std::string::npos) return std::fabs(d - std::atof(r.c_str())) < 0.5;
        const std::string lo = r.substr(0, dots), hi = r.substr(dots + 2);
        return (lo.empty() || d >= std::atof(lo.c_str())) && (hi.empty() || d <= std::atof(hi.c_str()));
    }
    [[nodiscard]] bool selectorOptionsKnown(const Selector& sel) {
        for (const auto& [key, value] : sel.options) {
            if (key != "type" && key != "distance" && key != "limit") {
                fail("Falcon does not know the selector option '" + key + "' yet");
                return false;
            }
        }
        if (!sel.error.empty()) {
            fail(sel.error);
            return false;
        }
        return true;
    }
    // True if the selector picks the player (the only one there is); `report` prints the game's "No player was found" otherwise.
    bool picksPlayer(const std::string& text, bool report = true) {
        const Selector sel = selector(text);
        if (!selectorOptionsKnown(sel)) return false;
        bool yes = false;
        if (sel.kind == 'N') yes = lowerCase(sel.name) == lowerCase(s.services_.playerName);
        else if (sel.kind == 's' || sel.kind == 'p' || sel.kind == 'a' || sel.kind == 'r') yes = typePasses(sel, "player") && distancePasses(sel, s.player_.x, s.player_.y, s.player_.z);
        else yes = typePasses(sel, "player") && distancePasses(sel, s.player_.x, s.player_.y, s.player_.z);
        if (!yes && report) fail(tr("argument.entity.notfound.player"));
        return yes;
    }

    // ---- positions ----
    struct Pos {
        double x = 0, y = 0, z = 0;
        bool ok = true;
    };
    // Three words: absolute, ~ or ~n (relative to the player). `block`: whole blocks; otherwise the centre of the block for x and z when they are whole numbers.
    Pos position(const std::string& text, bool block) {
        Pos p;
        std::vector<std::string> words;
        std::size_t start = 0;
        while (start <= text.size()) {
            const std::size_t e = text.find(' ', start);
            words.push_back(text.substr(start, e == std::string::npos ? std::string::npos : e - start));
            if (e == std::string::npos) break;
            start = e + 1;
        }
        if (words.size() != 3) {
            p.ok = false;
            return p;
        }
        const double base[3] = {block ? std::floor(s.player_.x) : s.player_.x, block ? std::floor(s.player_.y) : s.player_.y, block ? std::floor(s.player_.z) : s.player_.z};
        double* out[3] = {&p.x, &p.y, &p.z};
        for (int i = 0; i < 3; ++i) {
            const std::string& w = words[static_cast<std::size_t>(i)];
            if (!w.empty() && w[0] == '^') {
                fail("Falcon does not know local coordinates (^) yet");
                p.ok = false;
                return p;
            }
            if (!w.empty() && w[0] == '~') {
                *out[i] = base[i] + (w.size() > 1 ? std::atof(w.c_str() + 1) : 0.0);
            } else {
                *out[i] = std::atof(w.c_str());
                if (!block && (i == 0 || i == 2) && w.find('.') == std::string::npos) *out[i] += 0.5;
            }
        }
        if (block) {
            p.x = std::floor(p.x);
            p.y = std::floor(p.y);
            p.z = std::floor(p.z);
        }
        return p;
    }

    // ---- blocks ----
    // "oak_stairs[facing=east]{...}" -> a block state; the NBT part is ignored.
    [[nodiscard]] std::optional<Block> blockState(std::string text) const {
        const std::size_t nbt = text.find('{');
        if (nbt != std::string::npos) text.erase(nbt);
        return s.registry_->parse(text);
    }
    // A block predicate for /fill ... replace: an id (any state), an id with properties, or #tag.
    [[nodiscard]] bool matches(Block b, const std::string& filter) const {
        if (filter.empty()) return true;
        if (filter[0] == '#') return s.blockTags_ && s.blockTags_->contains(filter, static_cast<std::size_t>(s.registry_->state(b).block));
        const std::size_t bracket = filter.find('[');
        const std::string id = stripNamespace(filter.substr(0, bracket));
        if (s.registry_->type(b).name != id) return false;
        if (bracket == std::string::npos) return true;
        std::string props = filter.substr(bracket + 1);
        if (!props.empty() && props.back() == ']') props.pop_back();
        std::size_t start = 0;
        while (start < props.size()) {
            std::size_t comma = props.find(',', start);
            if (comma == std::string::npos) comma = props.size();
            const std::string pair = props.substr(start, comma - start);
            start = comma + 1;
            const std::size_t eq = pair.find('=');
            if (eq != std::string::npos && s.registry_->get(b, pair.substr(0, eq)) != pair.substr(eq + 1)) return false;
        }
        return true;
    }

    // ---- the commands ----
    void run();
    void give();
    void clear();
    void kill();
    void gamemode();
    void teleport();
    void setblock();
    void fillBlocks();
    void gamerule();
    void timeCommand();
    void experience();
    void difficulty();
    void summon();
    void spawnpoint(bool world);
    void help();
    void itemReplace();
    void messageCommands();
};

void CommandRunner::run() {
    if (command == "give") return give();
    if (command == "clear") return clear();
    if (command == "kill") return kill();
    if (command == "gamemode") return gamemode();
    if (command == "defaultgamemode") {
        const std::string mode = arg("gamemode");
        say(tr("commands.defaultgamemode.success", {tr("gameMode." + mode)}));  // (a new world would start in it: Falcon keeps no such setting)
        return;
    }
    if (command == "teleport") return teleport();
    if (command == "setblock") return setblock();
    if (command == "fill") return fillBlocks();
    if (command == "gamerule") return gamerule();
    if (command == "experience") return experience();
    if (command == "difficulty") return difficulty();
    if (command == "summon") return summon();
    if (command == "setworldspawn") return spawnpoint(true);
    if (command == "spawnpoint") return spawnpoint(false);
    if (command == "help") return help();
    if (command == "item") return itemReplace();
    if (command == "say" || command == "me" || command == "msg" || command == "tellraw" || command == "teammsg") return messageCommands();
    if (command == "seed") {
        say(tr("commands.seed.success", {std::string("[") + kSection + "a" + std::to_string(s.info_.seed) + kSection + "r]"}));
        return;
    }
    if (command == "list") {
        say(tr("commands.list.players", {"1", "8", s.services_.playerName}));
        return;
    }
    if (command == "save-all") {
        s.save();
        say(tr("commands.save.success"));
        return;
    }
    if (command == "time") return timeCommand();
    if (command == "weather") return unsupported("/" + command);
    unsupported("/" + command);
}

void CommandRunner::give() {
    if (!picksPlayer(arg("targets"))) return;
    std::string id = arg("item");
    const std::size_t bracket = id.find_first_of("[{");
    if (bracket != std::string::npos) id.erase(bracket);
    const auto item = itemRegistry().find(id);
    if (!item || *item == Item::Air) return fail("Unknown item '" + id + "'");
    const int count = find("count") != nullptr ? std::atoi(arg("count").c_str()) : 1;
    const int stackSize = itemRegistry().item(*item).maxStack;
    if (count > stackSize * 100) return fail(tr("commands.give.failed.toomanyitems", {std::to_string(stackSize * 100), itemDisplayName(ItemStack(*item, 1), s.services_.language)}));
    int left = count;
    while (left > 0) {
        ItemStack stack(*item, std::min(left, stackSize));
        left -= stack.count;
        s.inventory_.add(stack);
        if (!stack.isEmpty()) s.dropFromPlayer(stack);  // (what does not fit is thrown on the ground, as the game does)
    }
    say(tr("commands.give.success.single", {std::to_string(count), "[" + itemDisplayName(ItemStack(*item, 1), s.services_.language) + "]", s.services_.playerName}));
}

void CommandRunner::clear() {
    if (find("targets") != nullptr && !picksPlayer(arg("targets"))) return;
    const std::string filter = arg("item");  // an item id (an item predicate may be a tag too: those are not known here)
    std::optional<Item> item;
    if (!filter.empty()) {
        if (filter[0] == '#') return fail("Falcon does not know item tags in /clear yet");
        item = itemRegistry().find(filter.substr(0, filter.find_first_of("[{")));
        if (!item) return fail("Unknown item '" + filter + "'");
    }
    const int limit = find("maxCount") != nullptr ? std::atoi(arg("maxCount").c_str()) : -1;
    int total = 0;
    for (int slot = 0; slot < Inventory::kSize; ++slot) {
        ItemStack& stack = s.inventory_.item(slot);
        if (stack.isEmpty() || (item && stack.item != *item)) continue;
        total += stack.count;
    }
    if (total == 0) return fail(tr("commands.clear.failed.single", {s.services_.playerName}));
    if (limit == 0) return say(tr("commands.clear.test.single", {std::to_string(total), s.services_.playerName}));
    int toRemove = limit < 0 ? total : std::min(limit, total), removed = 0;
    for (int slot = 0; slot < Inventory::kSize && toRemove > 0; ++slot) {
        ItemStack& stack = s.inventory_.item(slot);
        if (stack.isEmpty() || (item && stack.item != *item)) continue;
        const int take = std::min(stack.count, toRemove);
        stack.shrink(take);
        if (stack.isEmpty()) stack = ItemStack();
        toRemove -= take;
        removed += take;
    }
    say(tr("commands.clear.success.single", {std::to_string(removed), s.services_.playerName}));
}

void CommandRunner::kill() {
    const std::string text = find("targets") != nullptr ? arg("targets") : "@s";
    const Selector sel = selector(text);
    if (!selectorOptionsKnown(sel)) return;
    int killed = 0;
    std::string lastName;
    const bool everything = sel.kind == 'e' || sel.kind == 'n';
    const int limit = sel.options.count("limit") != 0 ? std::atoi(sel.options.at("limit").c_str()) : 1 << 30;
    if (picksPlayer(text, false) && killed < limit) {
        if (!s.dead_) {
            s.vitals_.health = 0.0f;
            s.die();
        }
        ++killed;
        lastName = s.services_.playerName;
    }
    if (everything) {
        for (ItemEntity& item : s.itemEntities_.items()) {
            if (item.stack.isEmpty() || killed >= limit || !typePasses(sel, "item") || !distancePasses(sel, item.x, item.y, item.z)) continue;
            item.stack = ItemStack();
            ++killed;
            lastName = tr("entity.minecraft.item");
        }
        if (s.physics_) {
            if (typePasses(sel, "tnt")) {
                const int n = s.physics_->killTnt([&](const PrimedTnt& e) { return killed < limit && distancePasses(sel, e.x, e.y, e.z); });
                killed += n;
                if (n > 0) lastName = tr("entity.minecraft.tnt");
            }
            if (typePasses(sel, "falling_block")) {
                const int n = s.physics_->killFalling([&](const FallingBlock& e) { return killed < limit && distancePasses(sel, e.x, e.y, e.z); });
                killed += n;
                if (n > 0) lastName = tr("entity.minecraft.falling_block");
            }
        }
        if (s.mobSim_) {  // the mobs: each is killed as a blow from nowhere would (drops and all)
            for (Mob& mob : s.mobSim_->mobs()) {
                if (mob.dead() || killed >= limit || !typePasses(sel, mob.species->id) || !distancePasses(sel, mob.x, mob.y, mob.z)) continue;
                mob.invulnerable = 0;
                s.mobSim_->hurt(mob.id, 1.0e6f, mob.x, mob.z, 0.0f, false);
                ++killed;
                lastName = tr("entity.minecraft." + std::string(mob.species->id));
            }
        }
    }
    if (killed == 0) return fail(tr("argument.entity.notfound.entity"));
    say(killed == 1 ? tr("commands.kill.success.single", {lastName}) : tr("commands.kill.success.multiple", {std::to_string(killed)}));
}

void CommandRunner::gamemode() {
    const std::string mode = arg("gamemode");
    const int type = mode == "survival" ? 0 : mode == "creative" ? 1 : mode == "adventure" ? 2 : 3;
    if (find("target") != nullptr && !picksPlayer(arg("target"))) return;
    s.setGameType(type);
    say(tr("commands.gamemode.success.self", {tr("gameMode." + mode)}));
}

void CommandRunner::teleport() {
    // teleport <location> | <destination> | <targets> <location> | <targets> <destination>, then optionally a rotation
    if (find("targets") != nullptr && !picksPlayer(arg("targets"))) return;
    Pos target;
    if (find("location") != nullptr) {
        target = position(arg("location"), false);
        if (!target.ok) return;
    } else if (find("destination") != nullptr) {
        const Selector dest = selector(arg("destination"));
        if (!selectorOptionsKnown(dest)) return;
        if (!picksPlayer(arg("destination"))) return;  // the only entity there is to go to is the player
        target = {s.player_.x, s.player_.y, s.player_.z, true};
    } else {
        return fail("Nothing to teleport to");
    }
    if (target.y < kMinY - 64 || target.y > kMaxY + 64) return fail(tr("commands.teleport.invalidPosition"));
    s.player_.setPosition(target.x, target.y, target.z);
    s.player_.vx = s.player_.vy = s.player_.vz = 0.0;
    s.player_.fallDistance = 0.0;
    s.holdForLoading();
    if (const std::string* rotation = find("rotation")) {
        const std::size_t space = rotation->find(' ');
        const auto rel = [&](const std::string& w, float base) { return !w.empty() && w[0] == '~' ? base + static_cast<float>(w.size() > 1 ? std::atof(w.c_str() + 1) : 0.0) : static_cast<float>(std::atof(w.c_str())); };
        s.player_.yaw = rel(rotation->substr(0, space), s.player_.yaw);
        s.player_.pitch = std::clamp(rel(space == std::string::npos ? "~" : rotation->substr(space + 1), s.player_.pitch), -90.0f, 90.0f);
    }
    say(tr("commands.teleport.success.location.single", {s.services_.playerName, decimals(target.x), decimals(target.y), decimals(target.z)}));
}

void CommandRunner::setblock() {
    const Pos p = position(arg("pos"), true);
    if (!p.ok) return;
    const auto block = blockState(arg("block"));
    if (!block) return fail("Unknown block type '" + arg("block") + "'");
    const int x = static_cast<int>(p.x), y = static_cast<int>(p.y), z = static_cast<int>(p.z);
    if (y < kMinY || y >= kMaxY) return fail(tr("argument.pos.outofworld"));
    if (!s.renderer_.level().isLoaded(x, z)) return fail(tr("argument.pos.unloaded"));
    const Block old = s.renderer_.level().block(x, y, z);
    const std::string mode = has("destroy") ? "destroy" : has("keep") ? "keep" : "replace";
    if (mode == "keep" && !s.registry_->isAir(old)) return fail(tr("commands.setblock.failed"));
    if (mode == "destroy" && !s.registry_->isAir(old)) s.breakBlockAt(x, y, z, false);
    if (old == *block && mode != "destroy") return fail(tr("commands.setblock.failed"));
    s.setBlock(x, y, z, *block);
    if (const Block placed = *block; s.support_ && s.support_->isLowerHalf(placed)) s.renderer_.setBlock(x, y + 1, z, s.support_->upperHalf(placed));  // (a tall plant or a door is two blocks)
    say(tr("commands.setblock.success", {std::to_string(x), std::to_string(y), std::to_string(z)}));
}

void CommandRunner::fillBlocks() {
    const Pos a = position(arg("from"), true), b = position(arg("to"), true);
    if (!a.ok || !b.ok) return;
    const auto block = blockState(arg("block"));
    if (!block) return fail("Unknown block type '" + arg("block") + "'");
    const int x0 = static_cast<int>(std::min(a.x, b.x)), x1 = static_cast<int>(std::max(a.x, b.x));
    const int y0 = static_cast<int>(std::min(a.y, b.y)), y1 = static_cast<int>(std::max(a.y, b.y));
    const int z0 = static_cast<int>(std::min(a.z, b.z)), z1 = static_cast<int>(std::max(a.z, b.z));
    if (y0 < kMinY || y1 >= kMaxY) return fail(tr("argument.pos.outofworld"));
    const long long volume = static_cast<long long>(x1 - x0 + 1) * (y1 - y0 + 1) * (z1 - z0 + 1);
    const std::string maxText = s.gameRules_.count("max_block_modifications") != 0 ? s.gameRules_.at("max_block_modifications") : "32768";
    if (volume > std::atoll(maxText.c_str())) return fail(tr("commands.fill.toobig", {maxText, std::to_string(volume)}));
    for (int x = x0; x <= x1; x += 16) {
        for (int z = z0; z <= z1; z += 16) {
            if (!s.renderer_.level().isLoaded(x, z) || !s.renderer_.level().isLoaded(std::min(x + 15, x1), std::min(z + 15, z1))) return fail(tr("argument.pos.unloaded"));
        }
    }
    const std::string mode = has("destroy") ? "destroy" : has("hollow") ? "hollow" : has("keep") ? "keep" : has("outline") ? "outline" : "replace";
    const std::string filter = arg("filter");
    const Block air = Block::Air;
    int changed = 0;
    s.renderer_.beginBatch();
    for (int y = y0; y <= y1; ++y) {
        for (int z = z0; z <= z1; ++z) {
            for (int x = x0; x <= x1; ++x) {
                const bool edge = x == x0 || x == x1 || y == y0 || y == y1 || z == z0 || z == z1;
                const Block old = s.renderer_.level().block(x, y, z);
                Block now = *block;
                if (mode == "hollow" && !edge) now = air;
                else if (mode == "outline" && !edge) continue;
                else if (mode == "keep" && !s.registry_->isAir(old)) continue;
                else if (mode == "replace" && !filter.empty() && !matches(old, filter)) continue;
                if (old == now) continue;
                if (mode == "destroy" && !s.registry_->isAir(old)) s.breakBlockAt(x, y, z, false);
                if (s.setBlock(x, y, z, now)) ++changed;
            }
        }
    }
    s.renderer_.endBatch();
    if (changed == 0) return fail(tr("commands.fill.failed"));
    say(tr("commands.fill.success", {std::to_string(changed)}));
}

// /time set <time|marker>, add <time>, query <time|gametime|timeline>, pause, resume, rate <factor> (and the same after `of <clock>`: the Overworld's clock is the only one that runs)
void CommandRunner::timeCommand() {
    const std::string clock = "minecraft:overworld";
    if (has("pause")) {
        if (s.timePaused_) return fail(tr("commands.time.pause.already_paused", {clock}));
        s.timePaused_ = true;
        return say(tr("commands.time.pause", {clock}));
    }
    if (has("resume")) {
        if (!s.timePaused_) return fail(tr("commands.time.pause.already_running", {clock}));
        s.timePaused_ = false;
        return say(tr("commands.time.resume", {clock}));
    }
    if (has("rate")) {
        const double rate = std::atof(arg("rate").c_str());
        if (rate == s.timeRate_) return fail(tr("commands.time.rate.already_same", {clock, arg("rate")}));
        s.timeRate_ = rate;
        return say(tr("commands.time.rate", {clock, arg("rate")}));
    }
    if (has("query")) {
        if (has("gametime")) return say(tr("commands.time.query.gametime", {std::to_string(s.gameTime_)}));
        if (has("timeline")) {
            if (has("repetition")) return say(tr("commands.time.query.timeline.repetitions", {arg("timeline"), std::to_string(s.dayTime_ / daycycle::kTicksPerDay)}));
            return say(tr("commands.time.query.timeline", {arg("timeline"), std::to_string(s.dayTime_ % daycycle::kTicksPerDay)}));
        }
        return say(tr("commands.time.query", {std::to_string(s.dayTime_ % daycycle::kTicksPerDay)}));
    }
    long long ticks = 0;
    if (find("timemarker") != nullptr) {
        std::string marker = arg("timemarker");
        if (marker.rfind("minecraft:", 0) == 0) marker.erase(0, 10);
        if (marker == "day") ticks = 1000;
        else if (marker == "noon") ticks = 6000;
        else if (marker == "night") ticks = 13000;
        else if (marker == "midnight" || marker == "roll_village_siege") ticks = 18000;
        else if (marker == "wake_up_from_sleep") ticks = 0;
        else return fail(tr("commands.time.no_time_marker_found", {arg("timemarker"), clock}));
        // A marker is the next time of day that has it, not a day number: the day count stays.
        s.dayTime_ = s.dayTime_ - s.dayTime_ % daycycle::kTicksPerDay + ticks;
        if (s.dayTime_ < 0) s.dayTime_ = ticks;
        return say(tr("commands.time.set", {std::to_string(s.dayTime_)}));
    }
    const auto parsed = daycycle::parseTime(arg("time"));
    if (!parsed) return fail("Invalid time '" + arg("time") + "'");
    ticks = *parsed;
    if (has("add")) s.dayTime_ += ticks;
    else s.dayTime_ = ticks;
    say(tr("commands.time.set", {std::to_string(s.dayTime_)}));
}

void CommandRunner::gamerule() {
    // gamerule <rule> [<value>]: the rule is the literal typed (with or without "minecraft:")
    std::string rule;
    for (const std::string& word : literals) {
        if (word != "gamerule") rule = stripNamespace(word);
    }
    if (gameRuleDefaults().count(rule) == 0) return fail("Unknown game rule '" + rule + "'");
    const std::string* value = find("value");
    if (value == nullptr) {
        const std::string now = s.gameRules_.count(rule) != 0 ? s.gameRules_.at(rule) : gameRuleDefaults().at(rule);
        return say(tr("commands.gamerule.query", {rule, now}));
    }
    s.gameRules_[rule] = *value;
    s.applyGameRule(rule, *value);
    say(tr("commands.gamerule.set", {rule, *value}));
}

namespace {
int xpForNextLevel(int level) { return level >= 30 ? 112 + (level - 30) * 9 : level >= 15 ? 37 + (level - 15) * 5 : 7 + level * 2; }
int pointsAtLevel(int level) {
    int total = 0;
    for (int l = 0; l < level; ++l) total += xpForNextLevel(l);
    return total;
}
}  // namespace

void CommandRunner::experience() {
    if (!picksPlayer(arg("target"))) return;
    const bool levels = has("levels");
    Vitals& v = s.vitals_;
    const int totalNow = pointsAtLevel(v.level) + static_cast<int>(v.progress * static_cast<float>(xpForNextLevel(v.level)));
    const auto setTotal = [&](int total) {
        total = std::max(total, 0);
        int level = 0;
        while (total >= xpForNextLevel(level)) total -= xpForNextLevel(level++);
        v.level = level;
        v.progress = static_cast<float>(total) / static_cast<float>(xpForNextLevel(level));
    };
    if (has("query")) {
        return say(levels ? tr("commands.experience.query.levels", {s.services_.playerName, std::to_string(v.level)})
                          : tr("commands.experience.query.points", {s.services_.playerName, std::to_string(totalNow)}));
    }
    const int amount = std::atoi(arg("amount").c_str());
    if (has("set")) {
        if (amount < 0) return fail(tr("commands.experience.set.points.invalid"));
        if (levels) {
            v.level = amount;
            v.progress = 0.0f;
        } else {
            setTotal(amount);
        }
        return say(levels ? tr("commands.experience.set.levels.success.single", {std::to_string(amount), s.services_.playerName})
                          : tr("commands.experience.set.points.success.single", {std::to_string(amount), s.services_.playerName}));
    }
    if (levels) v.level = std::max(0, v.level + amount);
    else setTotal(totalNow + amount);
    say(levels ? tr("commands.experience.add.levels.success.single", {std::to_string(amount), s.services_.playerName})
               : tr("commands.experience.add.points.success.single", {std::to_string(amount), s.services_.playerName}));
}

void CommandRunner::difficulty() {
    static const char* kNames[] = {"peaceful", "easy", "normal", "hard"};
    std::string level;
    for (const std::string& word : literals) {
        for (const char* name : kNames) {
            if (word == name) level = word;
        }
    }
    if (level.empty()) return say(tr("commands.difficulty.query", {tr(std::string("options.difficulty.") + kNames[s.difficulty_])}));
    for (int i = 0; i < 4; ++i) {
        if (level == kNames[i]) s.difficulty_ = i;
    }
    say(tr("commands.difficulty.success", {tr("options.difficulty." + level)}));
}

void CommandRunner::summon() {
    const std::string entity = stripNamespace(arg("entity"));
    Pos at = find("pos") != nullptr ? position(arg("pos"), false) : Pos{s.player_.x, s.player_.y, s.player_.z, true};
    if (!at.ok) return;
    const std::string nbt = arg("nbt");
    if (entity == "tnt") {
        if (!s.physics_ || !s.physics_->tntExplodes()) return fail("TNT cannot be summoned: the game rule tnt_explodes is off");
        int fuse = 80;
        if (const std::size_t f = nbt.find("fuse:"); f != std::string::npos) fuse = std::atoi(nbt.c_str() + f + 5);
        s.physics_->spawnTnt(at.x, at.y, at.z, fuse);
    } else if (entity == "falling_block") {
        std::string name = "sand";
        if (const std::size_t n = nbt.find("Name:"); n != std::string::npos) {
            std::size_t q = nbt.find_first_of("\"'", n);
            if (q != std::string::npos) {
                const std::size_t e = nbt.find(nbt[q], q + 1);
                if (e != std::string::npos) name = nbt.substr(q + 1, e - q - 1);
            }
        }
        const auto block = s.registry_->find(name);
        if (!block || !s.physics_) return fail("Unknown block type '" + name + "'");
        s.physics_->spawnFalling(*block, at.x, at.y, at.z);
    } else if (entity == "item") {
        std::string id = "stone";
        int count = 1;
        if (const std::size_t n = nbt.find("id:"); n != std::string::npos) {
            std::size_t q = nbt.find_first_of("\"'", n);
            if (q != std::string::npos) {
                const std::size_t e = nbt.find(nbt[q], q + 1);
                if (e != std::string::npos) id = nbt.substr(q + 1, e - q - 1);
            }
        }
        if (const std::size_t c = nbt.find("count:"); c != std::string::npos) count = std::max(1, std::atoi(nbt.c_str() + c + 6));
        const auto item = itemRegistry().find(id);
        if (!item || *item == Item::Air) return fail("Unknown item '" + id + "'");
        s.itemEntities_.spawn(ItemStack(*item, count), at.x, at.y, at.z, 0.0, 0.0, 0.0, 10, s.randomFloat() * 6.2831855f);
    } else if (s.mobSim_ && mobSpecies(entity) != nullptr) {
        if (Mob* mob = s.mobSim_->spawn(entity, at.x, at.y, at.z, true)) mob->yaw = mob->yawo = s.randomFloat() * 360.0f - 180.0f;
    } else {
        return fail("Falcon has no " + tr("entity.minecraft." + entity) + " yet");
    }
    say(tr("commands.summon.success", {tr("entity.minecraft." + entity)}));
}

void CommandRunner::spawnpoint(bool world) {
    Pos at = find("pos") != nullptr ? position(arg("pos"), true) : Pos{std::floor(s.player_.x), std::floor(s.player_.y), std::floor(s.player_.z), true};
    if (!at.ok) return;
    if (!world && find("targets") != nullptr && !picksPlayer(arg("targets"))) return;
    s.spawn_ = {at.x + 0.5, at.y, at.z + 0.5};
    const std::string rotation = arg("rotation");
    const std::string angle = rotation.empty() ? "0.0" : rotation.substr(0, rotation.find(' '));
    if (world) return say(tr("commands.setworldspawn.success", {std::to_string(static_cast<int>(at.x)), std::to_string(static_cast<int>(at.y)), std::to_string(static_cast<int>(at.z)), angle}));
    say(tr("commands.spawnpoint.success.single", {std::to_string(static_cast<int>(at.x)), std::to_string(static_cast<int>(at.y)), std::to_string(static_cast<int>(at.z)), angle, "minecraft:overworld", s.services_.playerName}));
}

void CommandRunner::help() {
    const CommandTree& tree = *s.commands_;
    if (const std::string* name = find("command")) {
        std::string first = stripNamespace(name->substr(0, name->find(' ')));
        const CommandNode* node = tree.resolve({first});
        if (node == nullptr || (node->restricted && !s.info_.allowCommands)) return fail(tr("commands.help.failed"));
        const CommandNode& target = tree.continuation(*node);
        for (const auto& child : target.children) {
            if (child->restricted && !s.info_.allowCommands) continue;
            say("/" + first + " " + child->usageText() + (child->children.empty() ? "" : " ..."));
        }
        return say(commandUsageLine(tree, *node, s.info_.allowCommands));
    }
    for (const auto& root : tree.root().children) {
        if (root->restricted && !s.info_.allowCommands) continue;
        say(commandUsageLine(tree, *root, s.info_.allowCommands));
    }
}

// item replace entity <targets> <slot> with <item> [<count>]
void CommandRunner::itemReplace() {
    if (!has("replace") || !has("entity") || !has("with")) return unsupported("/item with that form");
    if (!picksPlayer(arg("target"))) return;
    const std::string slotName = arg("slots");
    std::string id = arg("item");
    id.erase(id.find_first_of("[{") == std::string::npos ? id.size() : id.find_first_of("[{"));
    const auto item = itemRegistry().find(id);
    if (!item) return fail("Unknown item '" + id + "'");
    int slot = -1;
    const auto number = [&](const std::string& prefix) { return slotName.rfind(prefix, 0) == 0 ? std::atoi(slotName.c_str() + prefix.size()) : -1; };
    if (slotName == "weapon" || slotName == "weapon.mainhand") slot = s.inventory_.selected;
    else if (slotName == "weapon.offhand") slot = Inventory::kOffhand;
    else if (slotName == "armor.feet") slot = Inventory::kFeet;
    else if (slotName == "armor.legs") slot = Inventory::kLegs;
    else if (slotName == "armor.chest") slot = Inventory::kChest;
    else if (slotName == "armor.head") slot = Inventory::kHead;
    else if (const int h = number("hotbar."); h >= 0 && h < 9) slot = h;
    else if (const int i = number("inventory."); i >= 0 && i < 27) slot = 9 + i;
    else if (const int c = number("container."); c >= 0 && c < 36) slot = c;
    if (slot < 0) return fail(tr("commands.item.target.no_such_slot", {slotName}));
    const int count = find("count") != nullptr ? std::atoi(arg("count").c_str()) : 1;
    ItemStack stack(*item, std::min(count, itemRegistry().item(*item).maxStack));
    s.inventory_.item(slot) = stack;
    say(tr("commands.item.entity.set.success.single", {s.services_.playerName, "[" + itemDisplayName(stack, s.services_.language) + "]"}));
}

void CommandRunner::messageCommands() {
    const std::string& name = s.services_.playerName;
    if (command == "say") return say(tr("chat.type.announcement", {name, arg("message")}));
    if (command == "me") return say(tr("chat.type.emote", {name, arg("action")}));
    if (command == "msg") {
        if (!picksPlayer(arg("targets"))) return;
        say(std::string(kSection) + "7" + kSection + "o" + tr("commands.message.display.outgoing", {name, arg("message")}));
        say(std::string(kSection) + "7" + kSection + "o" + tr("commands.message.display.incoming", {name, arg("message")}));
        return;
    }
    if (command == "tellraw") {
        if (!picksPlayer(arg("targets"))) return;
        const StyledText styled = parseChatComponentJson(arg("message"));
        if (styled.empty()) return fail("Invalid chat component");
        std::string line;
        for (const TextSpan& span : styled) {
            line += std::string(kSection) + "r";
            if (span.hasColor) {
                // The nearest of the sixteen colour codes.
                static const std::uint32_t kColours[16] = {0x000000, 0x0000AA, 0x00AA00, 0x00AAAA, 0xAA0000, 0xAA00AA, 0xFFAA00, 0xAAAAAA,
                                                          0x555555, 0x5555FF, 0x55FF55, 0x55FFFF, 0xFF5555, 0xFF55FF, 0xFFFF55, 0xFFFFFF};
                int best = 15;
                long long bestDistance = 1LL << 60;
                for (int i = 0; i < 16; ++i) {
                    const long long dr = static_cast<long long>((span.rgb >> 16) & 255) - ((kColours[i] >> 16) & 255), dg = static_cast<long long>((span.rgb >> 8) & 255) - ((kColours[i] >> 8) & 255),
                                    db = static_cast<long long>(span.rgb & 255) - (kColours[i] & 255);
                    const long long d = dr * dr + dg * dg + db * db;
                    if (d < bestDistance) {
                        bestDistance = d;
                        best = i;
                    }
                }
                line += std::string(kSection) + "0123456789abcdef"[best];
            }
            if (span.bold) line += std::string(kSection) + "l";
            if (span.italic) line += std::string(kSection) + "o";
            if (span.underlined) line += std::string(kSection) + "n";
            if (span.strikethrough) line += std::string(kSection) + "m";
            line += span.text;
        }
        return say(line);
    }
    unsupported("/" + command);
}

// ---------------------------------------------------------------------------------------------------------------
// GameSession

CommandContext GameSession::commandContext() const {
    CommandContext c;
    c.playerName = services_.playerName;
    c.cheats = info_.allowCommands;
    c.names = [this](const std::string& registry) -> const std::vector<std::string>& {
        auto it = registryNames_.find(registry);
        if (it != registryNames_.end()) return it->second;
        std::vector<std::string> names;
        if (registry == "minecraft:item") {
            for (const ItemType& t : itemRegistry().items()) {
                if (t.name != "air") names.push_back("minecraft:" + t.name);
            }
        } else if (registry == "minecraft:block") {
            for (const BlockType& t : registry_->blocks()) {
                if (t.name != "air" || true) names.push_back("minecraft:" + t.name);
            }
        } else if (registry == "minecraft:entity_type") {
            names = {"minecraft:falling_block", "minecraft:item", "minecraft:tnt"};  // (what Falcon can summon)
        }
        std::sort(names.begin(), names.end());
        return registryNames_.emplace(registry, std::move(names)).first->second;
    };
    return c;
}

void GameSession::applyGameRule(const std::string& rule, const std::string& value) {
    const bool on = value == "true";
    if (rule == "random_tick_speed" && physics_) physics_->setRandomTickSpeed(std::atoi(value.c_str()));
    else if (rule == "tnt_explodes" && physics_) physics_->setTntExplodes(on);
    else if (rule == "fire_spread_radius_around_player" && physics_) physics_->setFireSpreadRadius(std::atoi(value.c_str()));
}

bool GameSession::gameRuleOn(const std::string& rule) const {
    const auto it = gameRules_.find(rule);
    if (it != gameRules_.end()) return it->second == "true";
    return gameRuleDefaults().count(rule) != 0 && gameRuleDefaults().at(rule) == "true";
}

void GameSession::runCommand(const std::string& text) {
    if (!commands_) {
        addChatMessage(std::string(kSection) + "cThe game's command list is missing, so Falcon cannot run commands");
        return;
    }
    const CommandContext context = commandContext();
    const CommandParse parse = parseCommand(*commands_, context, text);
    if (!parse.complete) {
        // The game's own report: the error in red, then the command up to the mistake with a marker at it.
        const std::string message = parse.error.rfind("command.", 0) == 0 ? services_.language.get(parse.error) : parse.error;
        addChatMessage(std::string(kSection) + "c" + message);
        // (CommandSuggestions / Commands.performCommand: the last ten characters before the mistake, the rest underlined, then the marker)
        const std::string full = "/" + text;
        const std::size_t cursor = std::min(parse.errorAt + 1, full.size());
        const std::size_t from = cursor > 10 ? cursor - 10 : 0;
        std::string line = std::string(kSection) + "7" + (cursor > 10 ? "..." : "") + full.substr(from, cursor - from);
        if (cursor < full.size()) line += std::string(kSection) + "c" + kSection + "n" + full.substr(cursor);
        line += std::string(kSection) + "c" + kSection + "o" + services_.language.get("command.context.here");
        addChatMessage(line);
        return;
    }
    if (parse.parts.front().node->name == "execute") {
        // /execute: only the modifiers that mean something with one player here (as, at) and the command after "run" are carried out.
        CommandRunner check{*this, {}, {}, "execute"};
        std::size_t runAt = std::string::npos;
        for (const CommandPart& part : parse.parts) {
            const std::string word = text.substr(part.start, part.end - part.start);
            if (part.node->redirectsToRoot) {
                runAt = part.end;
                break;
            }
            if (part.node->isArgument()) {
                if (!check.picksPlayer(word)) return;  // (as or at somebody who is not there)
            } else if (word != "execute" && word != "as" && word != "at") {
                check.fail("Falcon cannot run /execute " + word + " yet");
                return;
            }
        }
        if (runAt == std::string::npos || runAt + 1 >= text.size()) return addChatMessage(std::string(kSection) + "cFalcon can only run /execute as, at and run");
        return runCommand(text.substr(runAt + 1));
    }
    CommandRunner runner{*this, {}, {}, {}};
    for (const CommandPart& part : parse.parts) {
        const std::string word = text.substr(part.start, part.end - part.start);
        if (part.node->isArgument()) runner.args.emplace_back(part.node->name, word);
        else runner.literals.push_back(word);
    }
    const CommandNode& first = *parse.parts.front().node;
    runner.command = first.redirect.empty() ? first.name : first.redirect.front();
    runner.run();
}

}  // namespace falcon
