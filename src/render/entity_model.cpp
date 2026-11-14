#include "apex/render/entity_model.hpp"

#include <nlohmann/json.hpp>

#include <fstream>
#include <iterator>

namespace apex {

namespace fs = std::filesystem;
using json = nlohmann::json;

EntityPart* EntityPart::find(const std::string& childName) {
    for (EntityPart& c : children) {
        if (c.name == childName) return &c;
    }
    return nullptr;
}

namespace {

EntityPart parsePart(const std::string& name, const json& j) {
    EntityPart part;
    part.name = name;
    if (const auto pose = j.find("pose"); pose != j.end() && pose->is_array() && pose->size() >= 9) {
        for (int i = 0; i < 3; ++i) {
            part.offset[static_cast<std::size_t>(i)] = (*pose)[static_cast<std::size_t>(i)].get<float>();
            part.rotation[static_cast<std::size_t>(i)] = (*pose)[static_cast<std::size_t>(3 + i)].get<float>();
            part.scale[static_cast<std::size_t>(i)] = (*pose)[static_cast<std::size_t>(6 + i)].get<float>();
        }
    }
    const auto polygons = j.find("polygons");
    const auto normals = j.find("normals");
    if (polygons != j.end() && polygons->is_array()) {
        for (std::size_t p = 0; p < polygons->size(); ++p) {
            const json& poly = (*polygons)[p];
            if (!poly.is_array() || poly.size() != 4) continue;
            EntityPolygon out;
            for (std::size_t v = 0; v < 4; ++v) {
                for (std::size_t k = 0; k < 5; ++k) out.vertex[v][k] = poly[v][k].get<float>();
            }
            if (normals != j.end() && normals->is_array() && p < normals->size()) {
                for (std::size_t k = 0; k < 3; ++k) out.normal[k] = (*normals)[p][k].get<float>();
            }
            part.polygons.push_back(out);
        }
    }
    if (const auto children = j.find("children"); children != j.end() && children->is_object()) {
        for (auto it = children->begin(); it != children->end(); ++it) part.children.push_back(parsePart(it.key(), it.value()));
    }
    return part;
}

}  // namespace

std::optional<EntityPart> loadEntityModel(const fs::path& dir, const std::string& key, std::string& error) {
    std::ifstream in(dir / "entity-models.json", std::ios::binary);
    if (!in) {
        error = "entity-models.json is not in " + dir.string();
        return std::nullopt;
    }
    const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    // The file is {"<model>": {...}, ...}: find this model's object and take it by balancing the braces (nothing in it is a string
    // with a brace).
    const std::string needle = "\"" + key + "\":{";
    const std::size_t at = text.find(needle);
    if (at == std::string::npos) {
        error = "the model " + key + " is not in entity-models.json";
        return std::nullopt;
    }
    const std::size_t start = at + needle.size() - 1;
    int depth = 0;
    std::size_t end = start;
    for (; end < text.size(); ++end) {
        if (text[end] == '{') ++depth;
        else if (text[end] == '}' && --depth == 0) break;
    }
    if (depth != 0) {
        error = "the model " + key + " is cut off";
        return std::nullopt;
    }
    const json doc = json::parse(text.begin() + static_cast<std::ptrdiff_t>(start), text.begin() + static_cast<std::ptrdiff_t>(end + 1), nullptr, /*allow_exceptions=*/false);
    if (!doc.is_object()) {
        error = "the model " + key + " is unreadable";
        return std::nullopt;
    }
    return parsePart("root", doc);
}

}  // namespace apex
