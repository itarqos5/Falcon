#include "falcon/game/item_names.hpp"

#include <cmath>
#include <cstdio>

namespace falcon {
namespace {

// Replaces the first "%s" of a translation with `value` (the game's %s / %1$s placeholders, which is all these strings use).
std::string fill(std::string pattern, const std::string& value) {
    const std::size_t at = pattern.find("%s");
    if (at != std::string::npos) pattern.replace(at, 2, value);
    return pattern;
}
std::string fill2(std::string pattern, const std::string& a, const std::string& b) {
    return fill(fill(std::move(pattern), a), b);
}

const char* slotKey(EquipSlot slot) {
    switch (slot) {
        case EquipSlot::Head: return "item.modifiers.head";
        case EquipSlot::Chest: return "item.modifiers.chest";
        case EquipSlot::Legs: return "item.modifiers.legs";
        case EquipSlot::Feet: return "item.modifiers.feet";
        case EquipSlot::Body: return "item.modifiers.body";
        default: return "item.modifiers.any";
    }
}

}  // namespace

std::string formatTooltipNumber(double value) {
    char buffer[32];
    std::snprintf(buffer, sizeof(buffer), "%.2f", std::round(value * 100.0) / 100.0);
    std::string s = buffer;
    while (!s.empty() && s.back() == '0') s.pop_back();
    if (!s.empty() && s.back() == '.') s.pop_back();
    return s.empty() ? "0" : s;
}

std::string itemDisplayName(const ItemStack& stack, const Language& language) {
    if (!stack.customName.empty()) return stack.customName;
    if (!stack.variantName.empty()) return stack.variantName;
    const ItemType& type = stack.type();
    return language.get(type.translationKey.empty() ? "item.minecraft." + type.name : type.translationKey);
}

std::vector<TooltipLine> itemTooltipLines(const ItemStack& stack, const Language& language, bool advanced, const std::vector<TooltipLine>& extra) {
    std::vector<TooltipLine> lines;
    if (stack.isEmpty()) return lines;
    const ItemType& type = stack.type();
    lines.push_back({itemDisplayName(stack, language), 0xFF000000u | rarityColor(type.rarity)});
    for (const TooltipLine& e : extra) lines.push_back(e);

    if (type.hasAttackModifiers) {
        lines.push_back({"", kTooltipGray});
        lines.push_back({language.get("item.modifiers.mainhand"), kTooltipGray});
        lines.push_back({" " + fill2(language.get("attribute.modifier.equals.0"), formatTooltipNumber(type.attackDamage()), language.get("attribute.name.attack_damage")),
                         kTooltipDarkGreen});
        lines.push_back({" " + fill2(language.get("attribute.modifier.equals.0"), formatTooltipNumber(type.attackSpeed()), language.get("attribute.name.attack_speed")),
                         kTooltipDarkGreen});
    }
    if (type.armor > 0.0f && type.armorSlot != EquipSlot::None) {
        lines.push_back({"", kTooltipGray});
        lines.push_back({language.get(slotKey(type.armorSlot)), kTooltipGray});
        lines.push_back({fill2(language.get("attribute.modifier.plus.0"), formatTooltipNumber(type.armor), language.get("attribute.name.armor")), kTooltipBlue});
        if (type.armorToughness > 0.0f) {
            lines.push_back({fill2(language.get("attribute.modifier.plus.0"), formatTooltipNumber(type.armorToughness), language.get("attribute.name.armor_toughness")),
                             kTooltipBlue});
        }
    }
    if (advanced) {
        if (type.maxDamage > 0 && stack.isDamaged()) {
            lines.push_back({fill2(language.get("item.durability"), std::to_string(type.maxDamage - stack.damage), std::to_string(type.maxDamage)), 0xFFFFFFFFu});
        }
        lines.push_back({"minecraft:" + type.name, kTooltipDarkGray});
    }
    return lines;
}

}  // namespace falcon
