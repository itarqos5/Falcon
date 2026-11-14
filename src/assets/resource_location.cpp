#include "falcon/assets/resource_location.hpp"

namespace falcon {

ResourceLocation ResourceLocation::parse(std::string_view text) {
    const auto colon = text.find(':');
    if (colon == std::string_view::npos) {
        return ResourceLocation("minecraft", std::string(text));
    }
    return ResourceLocation(std::string(text.substr(0, colon)), std::string(text.substr(colon + 1)));
}

ResourceLocation ResourceLocation::decorated(std::string_view prefix,
                                             std::string_view suffix) const {
    std::string p;
    p.reserve(prefix.size() + path.size() + suffix.size());
    p.append(prefix).append(path).append(suffix);
    return ResourceLocation(ns, std::move(p));
}

}  // namespace falcon
