#include "falcon/packs/pack_selection_model.hpp"

#include <algorithm>

namespace falcon {
namespace {

std::ptrdiff_t indexOf(const std::vector<std::string>& v, const std::string& id) {
    const auto it = std::find(v.begin(), v.end(), id);
    return it == v.end() ? -1 : it - v.begin();
}

}  // namespace

PackSelectionModel::PackSelectionModel(const PackRepository& repository,
                                       const std::vector<std::string>& savedIds)
    : repository_(repository) {
    // Saved ids are lowest priority first; the display list is the reverse (top = highest).
    for (auto it = savedIds.rbegin(); it != savedIds.rend(); ++it) {
        if (repository_.find(*it) != nullptr && indexOf(selected_, *it) < 0) selected_.push_back(*it);
    }
    // Required packs are always selected, at the bottom (lowest priority).
    for (const auto& [id, pack] : repository_.packs()) {
        if (pack.required && indexOf(selected_, id) < 0) selected_.push_back(id);
    }
    // Everything else is available, in id order.
    for (const auto& [id, pack] : repository_.packs()) {
        (void)pack;
        if (indexOf(selected_, id) < 0) available_.push_back(id);
    }
}

bool PackSelectionModel::isSelected(const std::string& id) const { return indexOf(selected_, id) >= 0; }

bool PackSelectionModel::canSelect(const std::string& id) const { return indexOf(available_, id) >= 0; }

bool PackSelectionModel::canUnselect(const std::string& id) const {
    const PackInfo* pack = repository_.find(id);
    return pack != nullptr && isSelected(id) && !pack->required;
}

bool PackSelectionModel::canMoveUp(const std::string& id) const { return indexOf(selected_, id) > 0; }

bool PackSelectionModel::canMoveDown(const std::string& id) const {
    const auto i = indexOf(selected_, id);
    return i >= 0 && i < static_cast<std::ptrdiff_t>(selected_.size()) - 1;
}

void PackSelectionModel::select(const std::string& id) {
    const auto i = indexOf(available_, id);
    if (i < 0) return;
    available_.erase(available_.begin() + i);
    selected_.insert(selected_.begin(), id);
}

void PackSelectionModel::unselect(const std::string& id) {
    if (!canUnselect(id)) return;
    selected_.erase(selected_.begin() + indexOf(selected_, id));
    available_.insert(available_.begin(), id);
}

void PackSelectionModel::moveUp(const std::string& id) {
    if (!canMoveUp(id)) return;
    const auto i = indexOf(selected_, id);
    std::swap(selected_[static_cast<std::size_t>(i)], selected_[static_cast<std::size_t>(i - 1)]);
}

void PackSelectionModel::moveDown(const std::string& id) {
    if (!canMoveDown(id)) return;
    const auto i = indexOf(selected_, id);
    std::swap(selected_[static_cast<std::size_t>(i)], selected_[static_cast<std::size_t>(i + 1)]);
}

std::vector<std::string> PackSelectionModel::commitIds() const {
    return std::vector<std::string>(selected_.rbegin(), selected_.rend());
}

}  // namespace falcon
