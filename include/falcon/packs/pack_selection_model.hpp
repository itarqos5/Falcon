#pragma once

#include "falcon/packs/pack_repository.hpp"

#include <string>
#include <vector>

namespace falcon {

// The two lists of the Resource Packs screen and the rules for moving packs between and within
// them (PackSelectionModel). `selected` is in display order: the top entry has the highest
// priority. The Default pack ("vanilla") is required, so it can never be unselected.
class PackSelectionModel {
public:
    // `savedIds` are the previously committed pack ids, lowest priority first.
    PackSelectionModel(const PackRepository& repository, const std::vector<std::string>& savedIds);

    [[nodiscard]] const std::vector<std::string>& selected() const { return selected_; }
    [[nodiscard]] const std::vector<std::string>& available() const { return available_; }

    [[nodiscard]] bool canSelect(const std::string& id) const;
    [[nodiscard]] bool canUnselect(const std::string& id) const;
    [[nodiscard]] bool canMoveUp(const std::string& id) const;
    [[nodiscard]] bool canMoveDown(const std::string& id) const;

    // Newly selected packs go to the top of Selected; unselected ones to the top of Available.
    void select(const std::string& id);
    void unselect(const std::string& id);
    void moveUp(const std::string& id);
    void moveDown(const std::string& id);

    // Selected ids, lowest priority first (the order the game stores in options.txt).
    [[nodiscard]] std::vector<std::string> commitIds() const;
    [[nodiscard]] bool isSelected(const std::string& id) const;

private:
    const PackRepository& repository_;
    std::vector<std::string> selected_;
    std::vector<std::string> available_;
};

}  // namespace falcon
