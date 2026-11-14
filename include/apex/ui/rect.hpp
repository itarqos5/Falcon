#pragma once

namespace apex {

// An integer rectangle in GUI units.
struct Rect {
    int x = 0;
    int y = 0;
    int width = 0;
    int height = 0;
    bool operator==(const Rect&) const = default;
};

}  // namespace apex
