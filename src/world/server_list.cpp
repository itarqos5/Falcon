#include "apex/world/server_list.hpp"

#include "apex/assets/nbt.hpp"
#include "apex/util/base64.hpp"
#include "apex/util/text_util.hpp"

#include <algorithm>
#include <fstream>
#include <iterator>

namespace apex {
namespace {

namespace fs = std::filesystem;

NbtTag toTag(const ServerEntry& e) {
    NbtTag tag = NbtTag::compound();
    tag.put("name", NbtTag::ofString(e.name));
    tag.put("ip", NbtTag::ofString(e.ip));
    if (!e.iconPng.empty()) tag.put("icon", NbtTag::ofString(base64Encode(e.iconPng)));
    // acceptTextures: 1 = Enabled, 0 = Disabled; absent = Prompt.
    if (e.packStatus != ServerPackStatus::Prompt) tag.put("acceptTextures", NbtTag::ofBool(e.packStatus == ServerPackStatus::Enabled));
    if (e.acceptedCodeOfConduct != 0) {
        tag.put("acceptedCodeOfConduct", NbtTag::ofInteger(NbtTag::Type::Int, e.acceptedCodeOfConduct));
    }
    tag.put("hidden", NbtTag::ofBool(e.hidden));
    return tag;
}

std::shared_ptr<ServerEntry> fromTag(const NbtTag& tag) {
    auto e = std::make_shared<ServerEntry>();
    e->name = tag.getString("name");
    e->ip = tag.getString("ip");
    if (const NbtTag* icon = tag.get("icon"); icon != nullptr && icon->type == NbtTag::Type::String) {
        if (auto bytes = base64Decode(icon->text)) e->iconPng = std::move(*bytes);
    }
    if (const NbtTag* accept = tag.get("acceptTextures"); accept != nullptr && accept->isNumber()) {
        e->packStatus = tag.getBool("acceptTextures") ? ServerPackStatus::Enabled : ServerPackStatus::Disabled;
    }
    e->acceptedCodeOfConduct = static_cast<int>(tag.getInt("acceptedCodeOfConduct", 0));
    e->hidden = tag.getBool("hidden");
    return e;
}

bool writeFile(const fs::path& file, const std::vector<unsigned char>& bytes) {
    std::ofstream out(file, std::ios::binary | std::ios::trunc);
    if (!out) return false;
    out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    out.flush();
    return static_cast<bool>(out);
}

}  // namespace

void ServerList::load() {
    visible_.clear();
    hidden_.clear();
    std::ifstream in(path(), std::ios::binary);
    if (!in) return;
    const std::vector<unsigned char> bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    const auto root = parseNbt(bytes);
    if (!root) return;
    const NbtTag* list = root->get("servers");
    if (list == nullptr || list->type != NbtTag::Type::List) return;
    for (const NbtTag& item : list->items) {
        if (item.type != NbtTag::Type::Compound) continue;  // anything else in the list is ignored
        auto entry = fromTag(item);
        (entry->hidden ? hidden_ : visible_).push_back(std::move(entry));
    }
}

bool ServerList::save() const {
    std::vector<NbtTag> items;
    for (const auto& e : visible_) {
        ServerEntry copy = *e;
        copy.hidden = false;
        items.push_back(toTag(copy));
    }
    for (const auto& e : hidden_) {
        ServerEntry copy = *e;
        copy.hidden = true;
        items.push_back(toTag(copy));
    }
    NbtTag root = NbtTag::compound();
    root.put("servers", NbtTag::ofList(std::move(items)));
    const std::vector<unsigned char> bytes = serializeNbt(root);  // plain NBT, not gzip

    std::error_code ec;
    fs::create_directories(gameDir_, ec);
    const fs::path target = path();
    const fs::path temp = gameDir_ / "servers.dat.tmp";
    const fs::path backup = gameDir_ / "servers.dat_old";
    if (!writeFile(temp, bytes)) {
        fs::remove(temp, ec);
        return false;
    }
    // Swap in the new file, keeping the previous one as servers.dat_old.
    const bool hadOld = fs::exists(target, ec);
    if (hadOld) {
        fs::remove(backup, ec);
        fs::rename(target, backup, ec);
        if (ec) {
            fs::remove(temp, ec);
            return false;
        }
    }
    fs::rename(temp, target, ec);
    if (ec) {
        std::error_code ignored;
        if (hadOld) fs::rename(backup, target, ignored);  // put the previous file back
        fs::remove(temp, ignored);
        return false;
    }
    return true;
}

void ServerList::add(std::shared_ptr<ServerEntry> entry, bool hidden) {
    entry->hidden = hidden;
    if (!hidden) {
        visible_.push_back(std::move(entry));
        return;
    }
    hidden_.insert(hidden_.begin(), std::move(entry));
    while (hidden_.size() > kMaxHidden) hidden_.pop_back();
}

std::shared_ptr<ServerEntry> ServerList::find(const std::string& ip) const {
    for (const auto& e : visible_) {
        if (e->ip == ip) return e;
    }
    for (const auto& e : hidden_) {
        if (e->ip == ip) return e;
    }
    return nullptr;
}

std::shared_ptr<ServerEntry> ServerList::unhide(const std::string& ip) {
    for (auto it = hidden_.begin(); it != hidden_.end(); ++it) {
        if ((*it)->ip != ip) continue;
        auto entry = *it;
        hidden_.erase(it);
        entry->hidden = false;
        visible_.push_back(entry);
        return entry;
    }
    return nullptr;
}

void ServerList::remove(const std::shared_ptr<ServerEntry>& entry) {
    if (auto it = std::find(visible_.begin(), visible_.end(), entry); it != visible_.end()) {
        visible_.erase(it);
    } else if (auto h = std::find(hidden_.begin(), hidden_.end(), entry); h != hidden_.end()) {
        hidden_.erase(h);
    }
}

void ServerList::swap(std::size_t a, std::size_t b) {
    if (a >= visible_.size() || b >= visible_.size()) return;
    std::swap(visible_[a], visible_[b]);
    save();
}

}  // namespace apex
