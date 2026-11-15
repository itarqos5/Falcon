#include "falcon/world/redstone.hpp"

#include <algorithm>
#include <cstdlib>
#include <deque>

namespace falcon {

namespace {

constexpr int kE = 0, kW = 1, kU = 2, kD = 3, kS = 4, kN = 5;  // the Face order: +X, -X, +Y, -Y, +Z, -Z
constexpr int kDx[6] = {1, -1, 0, 0, 0, 0}, kDy[6] = {0, 0, 1, -1, 0, 0}, kDz[6] = {0, 0, 0, 0, 1, -1};
constexpr const char* kFaceName[6] = {"east", "west", "up", "down", "south", "north"};
constexpr int kHorizontal[4] = {kN, kE, kS, kW};
int opp(int face) { return face ^ 1; }

bool startsWith(const std::string& s, const char* prefix) { return s.rfind(prefix, 0) == 0; }
bool endsWith(const std::string& s, const char* suffix) {
    const std::size_t n = std::char_traits<char>::length(suffix);
    return s.size() >= n && s.compare(s.size() - n, n, suffix) == 0;
}

// The sound family of a door, trapdoor or gate by what it is made of: "block.wooden_door.open", "block.iron_trapdoor.close", "block.fence_gate.open" ...
std::string openSound(const std::string& name, const char* what, bool open) {
    std::string family = "wooden";
    if (name.find("iron") != std::string::npos) family = "iron";
    else if (name.find("copper") != std::string::npos) family = "copper";
    else if (startsWith(name, "bamboo")) family = "bamboo_wood";
    else if (startsWith(name, "cherry")) family = "cherry_wood";
    else if (startsWith(name, "crimson") || startsWith(name, "warped")) family = "nether_wood";
    std::string kind = what;
    if (kind == "fence_gate") return std::string("block.") + (family == "wooden" ? "" : family + "_") + "fence_gate." + (open ? "open" : "close");
    return "block." + family + "_" + kind + (open ? ".open" : ".close");
}

}  // namespace

Redstone::Redstone(const Level& level, Host host, std::uint32_t seed) : level_(level), registry_(blockRegistry()), host_(std::move(host)), rng_(seed) {
    kinds_.assign(registry_.blocks().size(), kNone);
    for (std::size_t i = 0; i < kinds_.size(); ++i) {
        const std::string& n = registry_.blocks()[i].name;
        Kind k = kNone;
        if (n == "redstone_wire") {
            k = kWire;
            wireIndex_ = static_cast<int>(i);
        } else if (n == "redstone_torch") k = kTorch;
        else if (n == "redstone_wall_torch") k = kWallTorch;
        else if (n == "lever") k = kLever;
        else if (n == "stone_button" || n == "polished_blackstone_button") k = kStoneButton;
        else if (endsWith(n, "_button")) k = kWoodButton;
        else if (n == "stone_pressure_plate" || n == "polished_blackstone_pressure_plate") k = kStonePlate;
        else if (endsWith(n, "_pressure_plate") && n.find("weighted") == std::string::npos) k = kWoodPlate;
        else if (n == "redstone_lamp") k = kLamp;
        else if (n == "redstone_block") k = kRedstoneBlock;
        else if (n == "repeater") k = kRepeater;
        else if (n == "observer") k = kObserver;
        else if (n == "iron_door") k = kIronDoor;
        else if (endsWith(n, "_door")) k = kDoor;
        else if (n == "iron_trapdoor") k = kIronTrapdoor;
        else if (endsWith(n, "_trapdoor")) k = kTrapdoor;
        else if (endsWith(n, "_fence_gate")) k = kGate;
        else if (n == "tnt") k = kTnt;
        kinds_[i] = k;
    }
}

int Redstone::faceOf(Block b, const char* property) const {
    const std::string_view v = registry_.get(b, property);
    for (int f = 0; f < 6; ++f) {
        if (v == kFaceName[f]) return f;
    }
    return -1;
}

bool Redstone::sturdy(Block b) const {
    const StateInfo& s = registry_.state(b);
    return s.has(kStateFullCollision) || s.has(kStateOpaqueCube);
}

// BlockBehaviour.isRedstoneConductor: a solid block (not the block of redstone, not a machine part) carries power through itself.
bool Redstone::conductor(Block b) const {
    const Kind k = kindOf(b);
    return registry_.state(b).has(kStateOpaqueCube) && k != kRedstoneBlock && k != kObserver;
}

// The direction from a lever or button to the block it stands on is the opposite of this (FaceAttachedHorizontalDirectionalBlock.getConnectedDirection).
int Redstone::attachedFace(Block b) const {
    const std::string_view face = registry_.get(b, "face");
    if (face == "floor") return kU;
    if (face == "ceiling") return kD;
    return faceOf(b);
}

// What the block at (x, y, z) gives to a neighbour that lies the other way from `dir` (dir points from that neighbour to this block); `direct` is the strong
// kind that a solid block passes on to the blocks round it.
int Redstone::emitted(Block b, int, int, int, int dir, bool direct) const {
    switch (kindOf(b)) {
        case kRedstoneBlock: return direct ? 0 : 15;
        case kWire: {
            if (direct || !wiresGivePower_ || dir == kD) return 0;
            const int power = std::atoi(std::string(registry_.get(b, "power")).c_str());
            if (power == 0) return 0;
            if (dir == kU) return power;
            return registry_.get(b, kFaceName[opp(dir)]) != "none" ? power : 0;  // dust gives power along the lines it points
        }
        case kLever:
        case kStoneButton:
        case kWoodButton:
            if (!isTrue(b, "powered")) return 0;
            return !direct ? 15 : (attachedFace(b) == dir ? 15 : 0);
        case kStonePlate:
        case kWoodPlate:
            if (!isTrue(b, "powered")) return 0;
            return !direct ? 15 : (dir == kU ? 15 : 0);
        case kTorch:
            if (!isTrue(b, "lit")) return 0;
            return direct ? (dir == kD ? 15 : 0) : (dir != kU ? 15 : 0);
        case kWallTorch:
            if (!isTrue(b, "lit")) return 0;
            return direct ? (dir == kD ? 15 : 0) : (faceOf(b) != dir ? 15 : 0);
        case kRepeater:
        case kObserver:
            return isTrue(b, "powered") && faceOf(b) == dir ? 15 : 0;
        default: return 0;
    }
}

int Redstone::directSignalTo(int x, int y, int z) const {
    int best = 0;
    for (int d = 0; d < 6; ++d) {
        const int nx = x + kDx[d], ny = y + kDy[d], nz = z + kDz[d];
        best = std::max(best, emitted(block(nx, ny, nz), nx, ny, nz, d, true));
    }
    return best;
}

int Redstone::signalAt(int x, int y, int z, int dir) const {  // Level.getSignal(pos, dir)
    const Block b = block(x, y, z);
    const int own = emitted(b, x, y, z, dir, false);
    return conductor(b) ? std::max(own, directSignalTo(x, y, z)) : own;
}

int Redstone::bestNeighborSignal(int x, int y, int z) const {
    int best = 0;
    for (int d = 0; d < 6; ++d) best = std::max(best, signalAt(x + kDx[d], y + kDy[d], z + kDz[d], d));
    return best;
}

bool Redstone::hasNeighborSignal(int x, int y, int z) const {
    for (int d = 0; d < 6; ++d) {
        if (signalAt(x + kDx[d], y + kDy[d], z + kDz[d], d) > 0) return true;
    }
    return false;
}

// RedStoneWireBlock.calculateTargetStrength: the strongest signal given to it by anything but dust, or the strongest dust next to it (also up and down a step) less one.
int Redstone::wireTargetStrength(int x, int y, int z) const {
    wiresGivePower_ = false;
    const int fromOthers = bestNeighborSignal(x, y, z);
    wiresGivePower_ = true;
    int fromDust = 0;
    const auto wirePower = [&](Block b) { return kindOf(b) == kWire ? std::atoi(std::string(registry_.get(b, "power")).c_str()) : 0; };
    if (fromOthers < 15) {
        const bool aboveBlocks = conductor(block(x, y + 1, z));
        for (const int d : kHorizontal) {
            const int sx = x + kDx[d], sz = z + kDz[d];
            const Block side = block(sx, y, sz);
            fromDust = std::max(fromDust, wirePower(side));
            if (conductor(side) && !aboveBlocks) fromDust = std::max(fromDust, wirePower(block(sx, y + 1, sz)));
            else if (!conductor(side)) fromDust = std::max(fromDust, wirePower(block(sx, y - 1, sz)));
        }
    }
    return std::max(fromOthers, fromDust - 1);
}

// RedStoneWireBlock.shouldConnectTo: dust runs to dust, to a repeater that points along it, to an observer facing it, and to anything that can give a signal.
bool Redstone::connectsTo(Block b, int dir) const {
    switch (kindOf(b)) {
        case kWire: return true;
        case kRepeater: return dir >= 0 && (faceOf(b) == dir || opp(faceOf(b)) == dir);
        case kObserver: return dir >= 0 && faceOf(b) == dir;
        case kLever:
        case kStoneButton:
        case kWoodButton:
        case kStonePlate:
        case kWoodPlate:
        case kTorch:
        case kWallTorch:
        case kRedstoneBlock: return dir >= 0;
        default: return false;
    }
}

int Redstone::connectingSide(int x, int y, int z, int dir, bool canConnectUp) const {
    const int sx = x + kDx[dir], sz = z + kDz[dir];
    const Block side = block(sx, y, sz);
    if (canConnectUp) {
        const bool footing = endsWith(registry_.type(side).name, "_trapdoor") || sturdy(side);
        if (footing && connectsTo(block(sx, y + 1, sz), -1) && kindOf(block(sx, y + 1, sz)) == kWire) return sturdy(side) ? 2 : 1;
    }
    if (connectsTo(side, dir) || (!conductor(side) && kindOf(block(sx, y - 1, sz)) == kWire)) return 1;
    return 0;
}

// RedStoneWireBlock.getConnectionState: the four sides (none, side, up), with a straight line drawn through when it is only connected at one end.
Block Redstone::wireState(int x, int y, int z, Block current) const {
    const bool canConnectUp = !conductor(block(x, y + 1, z));
    int c[6] = {0, 0, 0, 0, 0, 0};
    for (const int d : kHorizontal) c[d] = connectingSide(x, y, z, d, canConnectUp);
    const bool wasDot = registry_.get(current, "north") == "none" && registry_.get(current, "east") == "none" && registry_.get(current, "south") == "none" && registry_.get(current, "west") == "none";
    const bool dotNow = !c[kN] && !c[kE] && !c[kS] && !c[kW];
    if (!(wasDot && dotNow)) {
        const bool n = c[kN] != 0, s = c[kS] != 0, e = c[kE] != 0, w = c[kW] != 0;
        const bool noNS = !n && !s, noEW = !e && !w;
        if (!w && noNS) c[kW] = 1;
        if (!e && noNS) c[kE] = 1;
        if (!n && noEW) c[kN] = 1;
        if (!s && noEW) c[kS] = 1;
    }
    Block state = current;
    static constexpr const char* kValue[3] = {"none", "side", "up"};
    for (const int d : kHorizontal) state = registry_.with(state, kFaceName[d], kValue[c[d]]);
    return state;
}

int Redstone::repeaterInput(int x, int y, int z, Block b) const {
    const int dir = faceOf(b);
    if (dir < 0) return 0;
    const int px = x + kDx[dir], py = y + kDy[dir], pz = z + kDz[dir];
    const int signal = signalAt(px, py, pz, dir);
    if (signal >= 15) return signal;
    const Block source = block(px, py, pz);
    return std::max(signal, kindOf(source) == kWire ? std::atoi(std::string(registry_.get(source, "power")).c_str()) : 0);
}

// ---------------------------------------------------------------------------------------------------------------
// Work

void Redstone::schedule(int x, int y, int z, int delay) {
    const std::array<int, 3> pos = {x, y, z};
    if (!scheduled_.insert(pos).second) return;
    due_.emplace(time_ + std::max(delay, 1), pos);
}

void Redstone::enqueue(int x, int y, int z) {
    if (y < kMinY || y >= kMaxY) return;
    const std::array<int, 3> pos = {x, y, z};
    if (queued_.insert(pos).second) queue_.push_back(pos);
}

void Redstone::process() {
    if (processing_) return;
    processing_ = true;
    std::size_t head = 0;
    int budget = 40000;
    while (head < queue_.size() && budget-- > 0) {
        const std::array<int, 3> pos = queue_[head++];
        queued_.erase(pos);
        evaluate(pos[0], pos[1], pos[2]);
        if (head > 4096 && head * 2 > queue_.size()) {  // (the front of the list that is done goes)
            queue_.erase(queue_.begin(), queue_.begin() + static_cast<std::ptrdiff_t>(head));
            head = 0;
        }
    }
    queue_.erase(queue_.begin(), queue_.begin() + static_cast<std::ptrdiff_t>(head));
    processing_ = false;
}

void Redstone::blockChanged(int x, int y, int z) {
    // An observer looking at the block sees it change.
    for (int d = 0; d < 6; ++d) {
        const int ox = x + kDx[d], oy = y + kDy[d], oz = z + kDz[d];
        const Block ob = block(ox, oy, oz);
        if (kindOf(ob) == kObserver && faceOf(ob) == opp(d) && !isTrue(ob, "powered")) schedule(ox, oy, oz, 2);
    }
    // What is near looks at itself again: the block, its neighbours, and theirs (a powered solid block gives its power to the blocks next to it).
    enqueue(x, y, z);
    for (int d = 0; d < 6; ++d) {
        enqueue(x + kDx[d], y + kDy[d], z + kDz[d]);
        for (int e = 0; e < 6; ++e) enqueue(x + kDx[d] + kDx[e], y + kDy[d] + kDy[e], z + kDz[d] + kDz[e]);
    }
    process();
}

void Redstone::setOpen(int x, int y, int z, Block b, bool open, bool powered, bool sound) {
    const Kind k = kindOf(b);
    const std::string& name = registry_.type(b).name;
    if (sound && isTrue(b, "open") != open) {
        const char* what = (k == kDoor || k == kIronDoor) ? "door" : (k == kTrapdoor || k == kIronTrapdoor) ? "trapdoor" : "fence_gate";
        snd(openSound(name, what, open), 1.0f, 0.9f + static_cast<float>(rng_() % 1000) / 10000.0f, x, y, z);
    }
    const auto apply = [&](Block from) { return registry_.with(registry_.with(from, "open", open ? "true" : "false"), "powered", powered ? "true" : "false"); };
    host_.setBlock(x, y, z, apply(b));
    if (k == kDoor || k == kIronDoor) {  // a door is two blocks that open together
        const int oy = registry_.get(b, "half") == "lower" ? y + 1 : y - 1;
        const Block other = block(x, oy, z);
        if (registry_.state(other).block == registry_.state(b).block) host_.setBlock(x, oy, z, apply(other));
    }
}

void Redstone::press(int x, int y, int z, Block b, bool on) {
    const Kind k = kindOf(b);
    host_.setBlock(x, y, z, registry_.with(b, "powered", on ? "true" : "false"));
    if (k == kStoneButton || k == kWoodButton) {
        snd(std::string("block.") + (k == kStoneButton ? "stone" : "wooden") + "_button.click_" + (on ? "on" : "off"), 0.3f, on ? 0.6f : 0.5f, x, y, z);
        if (on) schedule(x, y, z, k == kStoneButton ? 20 : 30);
    } else {
        snd(std::string("block.") + (k == kStonePlate ? "stone" : "wooden") + "_pressure_plate.click_" + (on ? "on" : "off"), 0.3f, on ? 0.6f : 0.5f, x, y, z);
    }
}

// PressurePlateBlock: pressed while something stands on it (a stone plate only feels players here, a wooden one items too).
void Redstone::plateCheck(int x, int y, int z) {
    const Block b = block(x, y, z);
    const Kind k = kindOf(b);
    if (k != kStonePlate && k != kWoodPlate) {
        plates_.erase({x, y, z});
        return;
    }
    const Aabb box{x + 0.0625, static_cast<double>(y), z + 0.0625, x + 0.9375, y + 0.25, z + 0.9375};
    const bool inside = host_.entityInside && host_.entityInside(box, k == kWoodPlate);
    const bool pressed = isTrue(b, "powered");
    if (inside != pressed) press(x, y, z, b, inside);
}

void Redstone::evaluate(int x, int y, int z) {
    const Block b = block(x, y, z);
    switch (kindOf(b)) {
        case kWire: {
            if (!sturdy(block(x, y - 1, z))) {  // dust needs a floor
                host_.setBlock(x, y, z, Block::Air);
                if (const auto item = itemRegistry().find("redstone"); item && host_.drop) host_.drop(ItemStack(*item, 1), x + 0.5, y + 0.3, z + 0.5);
                return;
            }
            const int power = wireTargetStrength(x, y, z);
            const Block next = registry_.with(wireState(x, y, z, b), "power", std::to_string(power));
            if (next != b) host_.setBlock(x, y, z, next);
            return;
        }
        case kTorch:
        case kWallTorch: {
            const bool wall = kindOf(b) == kWallTorch;
            const int f = wall ? opp(faceOf(b)) : kD;  // the way to the block it stands on
            const bool input = f >= 0 && signalAt(x + kDx[f], y + kDy[f], z + kDz[f], f) > 0;
            if (isTrue(b, "lit") == input) schedule(x, y, z, 2);
            return;
        }
        case kRepeater: {
            const bool should = repeaterInput(x, y, z, b) > 0;
            const int delay = std::atoi(std::string(registry_.get(b, "delay")).c_str());
            if (isTrue(b, "powered") != should) schedule(x, y, z, std::max(1, delay) * 2);
            return;
        }
        case kLamp: {
            const bool lit = isTrue(b, "lit"), should = hasNeighborSignal(x, y, z);
            if (lit && !should) schedule(x, y, z, 4);
            else if (!lit && should) host_.setBlock(x, y, z, registry_.with(b, "lit", "true"));
            return;
        }
        case kDoor:
        case kIronDoor: {
            const bool lower = registry_.get(b, "half") == "lower";
            const bool powered = hasNeighborSignal(x, y, z) || hasNeighborSignal(x, lower ? y + 1 : y - 1, z);
            if (powered != isTrue(b, "powered")) setOpen(x, y, z, b, powered, powered, true);
            return;
        }
        case kTrapdoor:
        case kIronTrapdoor:
        case kGate: {
            const bool powered = hasNeighborSignal(x, y, z);
            if (powered != isTrue(b, "powered")) setOpen(x, y, z, b, powered, powered, true);
            return;
        }
        case kTnt:
            if (hasNeighborSignal(x, y, z)) {
                host_.setBlock(x, y, z, Block::Air);
                if (host_.primeTnt) host_.primeTnt(x, y, z);
            }
            return;
        case kLever:
        case kStoneButton:
        case kWoodButton: {  // a lever or button needs the block it stands on
            const int connected = attachedFace(b);
            if (connected < 0) return;
            const int f = opp(connected);
            if (!sturdy(block(x + kDx[f], y + kDy[f], z + kDz[f]))) {
                host_.setBlock(x, y, z, Block::Air);
                const Item item = itemRegistry().itemOfBlock(registry_.state(b).block);
                if (item != Item::Air && host_.drop) host_.drop(ItemStack(item, 1), x + 0.5, y + 0.5, z + 0.5);
            }
            return;
        }
        case kStonePlate:
        case kWoodPlate: plates_.insert({x, y, z}); return;
        default: return;
    }
}

void Redstone::runScheduled(int x, int y, int z) {
    const Block b = block(x, y, z);
    switch (kindOf(b)) {
        case kTorch:
        case kWallTorch: {
            const bool wall = kindOf(b) == kWallTorch;
            const int f = wall ? opp(faceOf(b)) : kD;
            const bool input = f >= 0 && signalAt(x + kDx[f], y + kDy[f], z + kDz[f], f) > 0;
            const bool lit = isTrue(b, "lit");
            if (lit && input) host_.setBlock(x, y, z, registry_.with(b, "lit", "false"));
            else if (!lit && !input) host_.setBlock(x, y, z, registry_.with(b, "lit", "true"));
            return;
        }
        case kRepeater: {
            const bool powered = isTrue(b, "powered"), should = repeaterInput(x, y, z, b) > 0;
            const int delay = std::max(1, std::atoi(std::string(registry_.get(b, "delay")).c_str())) * 2;
            if (powered && !should) {
                host_.setBlock(x, y, z, registry_.with(b, "powered", "false"));
            } else if (!powered) {
                host_.setBlock(x, y, z, registry_.with(b, "powered", "true"));
                if (!should) schedule(x, y, z, delay);
            }
            return;
        }
        case kLamp:
            if (isTrue(b, "lit") && !hasNeighborSignal(x, y, z)) host_.setBlock(x, y, z, registry_.with(b, "lit", "false"));
            return;
        case kStoneButton:
        case kWoodButton:
            if (isTrue(b, "powered")) press(x, y, z, b, false);
            return;
        case kObserver:
            if (isTrue(b, "powered")) {
                host_.setBlock(x, y, z, registry_.with(b, "powered", "false"));
            } else {
                host_.setBlock(x, y, z, registry_.with(b, "powered", "true"));
                schedule(x, y, z, 2);
            }
            return;
        default: return;
    }
}

void Redstone::tick() {
    ++time_;
    while (!due_.empty() && due_.begin()->first <= time_) {
        const std::array<int, 3> pos = due_.begin()->second;
        due_.erase(due_.begin());
        scheduled_.erase(pos);
        if (level_.isLoaded(pos[0], pos[2])) runScheduled(pos[0], pos[1], pos[2]);
    }
    const std::set<std::array<int, 3>> plates = plates_;
    for (const std::array<int, 3>& p : plates) plateCheck(p[0], p[1], p[2]);
    process();
}

bool Redstone::use(int x, int y, int z) {
    const Block b = block(x, y, z);
    switch (kindOf(b)) {
        case kLever: {
            const bool on = !isTrue(b, "powered");
            host_.setBlock(x, y, z, registry_.with(b, "powered", on ? "true" : "false"));
            snd("block.lever.click", 0.3f, on ? 0.6f : 0.5f, x, y, z);
            return true;
        }
        case kStoneButton:
        case kWoodButton:
            if (!isTrue(b, "powered")) press(x, y, z, b, true);
            return true;
        case kDoor:
        case kTrapdoor:
        case kGate: setOpen(x, y, z, b, !isTrue(b, "open"), isTrue(b, "powered"), true); return true;
        default: return false;  // (an iron door or trapdoor only answers to power)
    }
}

}  // namespace falcon
