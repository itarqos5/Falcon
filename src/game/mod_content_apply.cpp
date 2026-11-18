#include "falcon/game/mod_content_apply.hpp"

#include <GLFW/glfw3.h>

#include <algorithm>

namespace falcon {
namespace {

// "examplemod:ruby" -> {"examplemod", "ruby"}
std::pair<std::string, std::string> split(const std::string& id) {
    const std::size_t colon = id.find(':');
    return colon == std::string::npos ? std::pair<std::string, std::string>{"minecraft", id} : std::pair<std::string, std::string>{id.substr(0, colon), id.substr(colon + 1)};
}

Rarity rarityOf(const std::string& name) {
    if (name == "uncommon") return Rarity::Uncommon;
    if (name == "rare") return Rarity::Rare;
    if (name == "epic") return Rarity::Epic;
    return Rarity::Common;
}

}  // namespace

std::vector<ExtraBlock> modExtraBlocks(const jvm::ModHost* host) {
    std::vector<ExtraBlock> out;
    if (host == nullptr) return out;
    for (const auto& mod : host->mods()) {
        for (const jvm::ModBlockDef& b : mod->content().blocks) {
            out.push_back(ExtraBlock{b.id, b.instabreak ? 0.0f : b.hardness, b.resistance, b.light, b.requiresTool, b.noOcclusion, b.sound});
        }
    }
    return out;
}

std::vector<ExtraItem> modExtraItems(const jvm::ModHost* host) {
    std::vector<ExtraItem> out;
    if (host == nullptr) return out;
    for (const auto& mod : host->mods()) {
        for (const jvm::ModItemDef& i : mod->content().items) {
            const auto [ns, path] = split(i.id);
            ExtraItem e;
            e.name = i.id;
            e.translationKey = (i.block.empty() ? "item." : "block.") + ns + "." + path;
            e.itemModel = i.id;
            e.maxStack = i.stacksTo;
            e.maxDamage = i.durability;
            e.rarity = rarityOf(i.rarity);
            if (i.food) e.food = FoodInfo{i.nutrition, i.saturation * static_cast<float>(i.nutrition) * 2.0f, false};  // (the game's builder takes a modifier: saturation = nutrition * modifier * 2)
            e.fireResistant = i.fireResistant;
            out.push_back(std::move(e));
        }
    }
    return out;
}

std::vector<CreativeTabs::ExtraEntry> modCreativeEntries(const jvm::ModHost* host, const Language& language) {
    std::vector<CreativeTabs::ExtraEntry> out;
    if (host == nullptr) return out;
    for (const ExtraItem& e : modExtraItems(host)) {
        for (const auto& mod : host->mods()) {
            for (const jvm::ModItemDef& i : mod->content().items) {
                if (i.id == e.name) out.push_back({i.tab, i.id, language.get(e.translationKey)});
            }
        }
    }
    return out;
}

std::vector<ModKeyMapping> modKeyMappings(const jvm::ModHost* host) {
    std::vector<ModKeyMapping> out;
    if (host == nullptr) return out;
    for (const auto& mod : host->mods()) {
        for (const jvm::ModKeyDef& k : mod->content().keys) {
            ModKeyMapping m;
            m.name = k.name;
            m.category = k.category;
            m.defaultKey = k.key < 0 ? KeyId::unbound() : k.mouse ? KeyId::mouse(k.key) : KeyId::keyboard(k.key);
            out.push_back(std::move(m));
        }
    }
    return out;
}

}  // namespace falcon
