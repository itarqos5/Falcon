#pragma once

#include "falcon/game/creative_tabs.hpp"
#include "falcon/game/key_mappings.hpp"
#include "falcon/jvm/mod_runtime.hpp"
#include "falcon/text/language.hpp"
#include "falcon/world/block_registry.hpp"
#include "falcon/world/item_registry.hpp"

#include <vector>

// What the mods added (jvm/mod_content.hpp), turned into what the game's own tables take: the blocks, items and creative entries the registries are extended with, and the keys the Key Binds
// screen lists. Nothing here runs a mod; it only reads what the VM found.
namespace falcon {

[[nodiscard]] std::vector<ExtraBlock> modExtraBlocks(const jvm::ModHost* host);
[[nodiscard]] std::vector<ExtraItem> modExtraItems(const jvm::ModHost* host);
// Every mod item in the tab its mod gave it, named by the language (for the search tab).
[[nodiscard]] std::vector<CreativeTabs::ExtraEntry> modCreativeEntries(const jvm::ModHost* host, const Language& language);
[[nodiscard]] std::vector<ModKeyMapping> modKeyMappings(const jvm::ModHost* host);

}  // namespace falcon
