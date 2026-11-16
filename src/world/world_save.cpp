#include "falcon/world/world_save.hpp"

#include "falcon/world/item_registry.hpp"

#include <algorithm>
#include <fstream>
#include <iterator>
#include <map>
#include <system_error>

namespace falcon {

namespace {

NbtTag real(NbtTag::Type type, double value) {
    NbtTag t;
    t.type = type;
    t.real = value;
    return t;
}
NbtTag dbl(double v) { return real(NbtTag::Type::Double, v); }
NbtTag flt(double v) { return real(NbtTag::Type::Float, v); }
NbtTag integer(std::int64_t v) { return NbtTag::ofInteger(NbtTag::Type::Int, v); }

std::uint32_t pack(int x, int y, int z) {
    return static_cast<std::uint32_t>(x & 15) | (static_cast<std::uint32_t>(z & 15) << 4) | (static_cast<std::uint32_t>(y - kMinY) << 8);
}

bool readFile(const std::filesystem::path& file, std::vector<unsigned char>& bytes) {
    std::ifstream in(file, std::ios::binary);
    if (!in) return false;
    bytes.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    return true;
}

}  // namespace

// ---------------------------------------------------------------------------------------------------------------
// Block edits

void WorldEdits::set(int x, int y, int z, Block block) {
    if (y < kMinY || y >= kMaxY) return;
    std::lock_guard<std::mutex> lock(mutex_);
    chunks_[{chunkOf(x), chunkOf(z)}][pack(x, y, z)] = static_cast<std::uint16_t>(block);
    changed_ = true;
}

void WorldEdits::applyTo(ChunkPos pos, Chunk& chunk) const {
    std::lock_guard<std::mutex> lock(mutex_);
    const auto found = chunks_.find(pos);
    if (found == chunks_.end()) return;
    for (const auto& [key, state] : found->second) {
        const int x = static_cast<int>(key & 15), z = static_cast<int>((key >> 4) & 15), y = static_cast<int>(key >> 8);
        SectionPtr& section = chunk.sections[static_cast<std::size_t>(y >> 4)];
        section = sectionWithBlock(section, x, y & 15, z, static_cast<Block>(state));
    }
}

std::size_t WorldEdits::size() const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::size_t n = 0;
    for (const auto& [pos, blocks] : chunks_) n += blocks.size();
    return n;
}

std::size_t WorldEdits::chunkCount() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return chunks_.size();
}

bool WorldEdits::changed() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return changed_;
}

NbtTag WorldEdits::toNbt(const BlockRegistry& registry) {
    std::lock_guard<std::mutex> lock(mutex_);
    std::map<std::uint16_t, int> paletteIndex;  // state -> index, in the order first met (stable for one save)
    std::vector<NbtTag> palette;
    std::vector<NbtTag> chunkTags;
    // In a fixed order, so saving twice without changes writes the same bytes.
    std::vector<ChunkPos> order;
    for (const auto& [pos, blocks] : chunks_) order.push_back(pos);
    std::sort(order.begin(), order.end(), [](const ChunkPos& a, const ChunkPos& b) { return a.x != b.x ? a.x < b.x : a.z < b.z; });
    for (const ChunkPos& pos : order) {
        const auto& blocks = chunks_.at(pos);
        std::vector<std::pair<std::uint32_t, std::uint16_t>> sorted(blocks.begin(), blocks.end());
        std::sort(sorted.begin(), sorted.end());
        NbtTag data;
        data.type = NbtTag::Type::IntArray;
        for (const auto& [key, state] : sorted) {
            auto it = paletteIndex.find(state);
            if (it == paletteIndex.end()) {
                it = paletteIndex.emplace(state, static_cast<int>(palette.size())).first;
                palette.push_back(NbtTag::ofString(registry.describe(static_cast<Block>(state))));
            }
            data.numbers.push_back(static_cast<std::int64_t>(key));
            data.numbers.push_back(it->second);
        }
        NbtTag chunkTag = NbtTag::compound();
        chunkTag.put("x", integer(pos.x)).put("z", integer(pos.z)).put("blocks", std::move(data));
        chunkTags.push_back(std::move(chunkTag));
    }
    NbtTag out = NbtTag::compound();
    out.put("palette", NbtTag::ofList(std::move(palette)));
    out.put("chunks", NbtTag::ofList(std::move(chunkTags)));
    changed_ = false;
    return out;
}

bool WorldEdits::fromNbt(const NbtTag& tag, const BlockRegistry& registry) {
    const NbtTag* palette = tag.get("palette");
    const NbtTag* chunks = tag.get("chunks");
    if (palette == nullptr || chunks == nullptr || palette->type != NbtTag::Type::List || chunks->type != NbtTag::Type::List) return false;
    std::vector<std::optional<std::uint16_t>> states;
    for (const NbtTag& name : palette->items) {
        const auto block = registry.parse(name.text);
        states.push_back(block ? std::optional<std::uint16_t>(static_cast<std::uint16_t>(*block)) : std::nullopt);
    }
    std::lock_guard<std::mutex> lock(mutex_);
    chunks_.clear();
    for (const NbtTag& c : chunks->items) {
        const NbtTag* data = c.get("blocks");
        if (data == nullptr || data->type != NbtTag::Type::IntArray) continue;
        auto& blocks = chunks_[{static_cast<int>(c.getInt("x")), static_cast<int>(c.getInt("z"))}];
        for (std::size_t i = 0; i + 1 < data->numbers.size(); i += 2) {
            const auto index = static_cast<std::size_t>(data->numbers[i + 1]);
            if (index < states.size() && states[index]) blocks[static_cast<std::uint32_t>(data->numbers[i])] = *states[index];
        }
        if (blocks.empty()) chunks_.erase({static_cast<int>(c.getInt("x")), static_cast<int>(c.getInt("z"))});
    }
    changed_ = false;
    return true;
}

// ---------------------------------------------------------------------------------------------------------------
// Items

NbtTag stackToNbt(const ItemStack& stack) {
    NbtTag t = NbtTag::compound();
    t.put("id", NbtTag::ofString(stack.type().name)).put("count", integer(stack.count)).put("damage", integer(stack.damage));
    if (!stack.customName.empty()) t.put("name", NbtTag::ofString(stack.customName));
    if (!stack.components.empty()) t.put("components", NbtTag::ofString(stack.components));
    if (!stack.variantName.empty()) t.put("variant", NbtTag::ofString(stack.variantName));
    return t;
}

ItemStack stackFromNbt(const NbtTag& tag) {
    const auto item = itemRegistry().find(tag.getString("id"));
    if (!item || *item == Item::Air) return ItemStack();
    ItemStack stack(*item, static_cast<int>(tag.getInt("count", 1)));
    stack.damage = static_cast<int>(tag.getInt("damage", 0));
    stack.customName = tag.getString("name");
    stack.components = tag.getString("components");
    stack.variantName = tag.getString("variant");
    return stack;
}

// ---------------------------------------------------------------------------------------------------------------
// The file

std::string writeWorldSave(const std::filesystem::path& file, const WorldSave& save, WorldEdits& edits, const BlockRegistry& registry) {
    NbtTag root = NbtTag::compound();
    root.put("version", integer(WorldSave::kVersion));
    if (save.hasPlayer) {
        const SavedPlayer& p = save.player;
        NbtTag player = NbtTag::compound();
        player.put("x", dbl(p.x)).put("y", dbl(p.y)).put("z", dbl(p.z));
        player.put("yaw", flt(static_cast<double>(p.yaw))).put("pitch", flt(static_cast<double>(p.pitch)));
        player.put("flying", NbtTag::ofBool(p.flying));
        player.put("health", flt(static_cast<double>(p.health))).put("food", integer(p.food)).put("saturation", flt(static_cast<double>(p.saturation)));
        player.put("level", integer(p.level)).put("progress", flt(static_cast<double>(p.progress)));
        player.put("selected", integer(p.selectedSlot)).put("gameType", integer(p.gameType));
        std::vector<NbtTag> slots;
        for (const auto& [slot, stack] : p.inventory) {
            NbtTag s = stackToNbt(stack);
            s.put("slot", integer(slot));
            slots.push_back(std::move(s));
        }
        player.put("inventory", NbtTag::ofList(std::move(slots)));
        root.put("player", std::move(player));
    }
    std::vector<NbtTag> items;
    for (const ItemEntity& e : save.items) {
        NbtTag t = stackToNbt(e.stack);
        t.put("x", dbl(e.x)).put("y", dbl(e.y)).put("z", dbl(e.z)).put("vx", dbl(e.vx)).put("vy", dbl(e.vy)).put("vz", dbl(e.vz));
        t.put("age", integer(e.age)).put("pickupDelay", integer(e.pickupDelay)).put("bob", flt(static_cast<double>(e.bobOffset)));
        items.push_back(std::move(t));
    }
    root.put("items", NbtTag::ofList(std::move(items)));
    if (save.containers.type == NbtTag::Type::List) root.put("containers", save.containers);
    {
        std::vector<NbtTag> ender;
        for (const auto& [slot, stack] : save.enderChest) {
            NbtTag s = stackToNbt(stack);
            s.put("slot", integer(slot));
            ender.push_back(std::move(s));
        }
        root.put("enderChest", NbtTag::ofList(std::move(ender)));
    }
    {
        NbtTag rules = NbtTag::compound();
        for (const auto& [name, value] : save.gameRules) rules.put(name, NbtTag::ofString(value));
        root.put("gameRules", std::move(rules));
        root.put("difficulty", integer(save.difficulty));
        root.put("dayTime", NbtTag::ofInteger(NbtTag::Type::Long, save.dayTime));
        root.put("gameTime", NbtTag::ofInteger(NbtTag::Type::Long, save.gameTime));
    }
    root.put("edits", edits.toNbt(registry));

    const std::vector<unsigned char> bytes = gzipStored(serializeNbt(root, ""));
    std::error_code ec;
    std::filesystem::create_directories(file.parent_path(), ec);
    const std::filesystem::path temp = file.parent_path() / (file.filename().string() + ".tmp");
    {
        std::ofstream out(temp, std::ios::binary | std::ios::trunc);
        if (!out) return "cannot write " + temp.string();
        out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
        out.flush();
        if (!out) return "cannot write " + temp.string();
    }
    // Keep the previous save as a spare, then put the new one in its place.
    if (std::filesystem::exists(file, ec)) {
        std::filesystem::copy_file(file, file.parent_path() / (file.filename().string() + "_old"), std::filesystem::copy_options::overwrite_existing, ec);
    }
    std::filesystem::rename(temp, file, ec);
    if (ec) return "cannot replace " + file.string() + ": " + ec.message();
    return {};
}

bool readWorldSave(const std::filesystem::path& file, WorldSave& save, WorldEdits& edits, const BlockRegistry& registry, std::string& error) {
    error.clear();
    const auto tryFile = [&](const std::filesystem::path& path, bool& existed) -> bool {
        std::vector<unsigned char> bytes;
        existed = readFile(path, bytes);
        if (!existed) return false;
        const auto root = parseNbt(bytes);
        if (!root || root->type != NbtTag::Type::Compound) return false;
        if (root->getInt("version", 0) > WorldSave::kVersion) return false;  // made by a newer Falcon
        WorldSave loaded;
        if (const NbtTag* p = root->get("player"); p != nullptr && p->type == NbtTag::Type::Compound) {
            loaded.hasPlayer = true;
            SavedPlayer& s = loaded.player;
            s.x = p->getDouble("x");
            s.y = p->getDouble("y");
            s.z = p->getDouble("z");
            s.yaw = static_cast<float>(p->getDouble("yaw"));
            s.pitch = static_cast<float>(p->getDouble("pitch"));
            s.flying = p->getBool("flying");
            s.health = static_cast<float>(p->getDouble("health", 20.0));
            s.food = static_cast<int>(p->getInt("food", 20));
            s.saturation = static_cast<float>(p->getDouble("saturation", 5.0));
            s.level = static_cast<int>(p->getInt("level", 0));
            s.progress = static_cast<float>(p->getDouble("progress", 0.0));
            s.selectedSlot = static_cast<int>(p->getInt("selected", 0));
            s.gameType = static_cast<int>(p->getInt("gameType", -1));
            if (const NbtTag* inv = p->get("inventory"); inv != nullptr) {
                for (const NbtTag& slot : inv->items) {
                    ItemStack stack = stackFromNbt(slot);
                    if (!stack.isEmpty()) s.inventory.emplace_back(static_cast<int>(slot.getInt("slot", 0)), std::move(stack));
                }
            }
        }
        if (const NbtTag* rules = root->get("gameRules"); rules != nullptr && rules->type == NbtTag::Type::Compound) {
            for (const auto& [name, value] : rules->fields) loaded.gameRules[name] = value.text;
        }
        loaded.difficulty = static_cast<int>(root->getInt("difficulty", -1));
        loaded.dayTime = root->getInt("dayTime", -1);
        loaded.gameTime = root->getInt("gameTime", 0);
        if (const NbtTag* items = root->get("items"); items != nullptr) {
            for (const NbtTag& t : items->items) {
                ItemStack stack = stackFromNbt(t);
                if (stack.isEmpty()) continue;
                ItemEntity e;
                e.stack = std::move(stack);
                e.x = e.xo = t.getDouble("x");
                e.y = e.yo = t.getDouble("y");
                e.z = e.zo = t.getDouble("z");
                e.vx = t.getDouble("vx");
                e.vy = t.getDouble("vy");
                e.vz = t.getDouble("vz");
                e.age = static_cast<int>(t.getInt("age", 0));
                e.pickupDelay = static_cast<int>(t.getInt("pickupDelay", 0));
                e.bobOffset = static_cast<float>(t.getDouble("bob"));
                loaded.items.push_back(std::move(e));
            }
        }
        if (const NbtTag* c = root->get("containers"); c != nullptr && c->type == NbtTag::Type::List) loaded.containers = *c;
        if (const NbtTag* ender = root->get("enderChest"); ender != nullptr) {
            for (const NbtTag& slot : ender->items) {
                ItemStack stack = stackFromNbt(slot);
                if (!stack.isEmpty()) loaded.enderChest.emplace_back(static_cast<int>(slot.getInt("slot", 0)), std::move(stack));
            }
        }
        if (const NbtTag* e = root->get("edits"); e != nullptr) {
            if (!edits.fromNbt(*e, registry)) return false;
        }
        save = std::move(loaded);
        return true;
    };
    bool existed = false, spareExisted = false;
    if (tryFile(file, existed)) return true;
    if (tryFile(file.parent_path() / (file.filename().string() + "_old"), spareExisted)) return true;  // (also what a crash between the copy and the rename leaves)
    if (!existed && !spareExisted) return false;  // no save yet
    error = "the save " + file.string() + " is damaged";
    return false;
}

}  // namespace falcon
