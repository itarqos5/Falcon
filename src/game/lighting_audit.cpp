#include "falcon/game/lighting_audit.hpp"

#include "falcon/render/block_models.hpp"
#include "falcon/world/block_registry.hpp"
#include "falcon/world/chunk.hpp"
#include "falcon/world/light.hpp"
#include "falcon/world/mesher.hpp"

#include <algorithm>
#include <cstdio>
#include <string>
#include <vector>

namespace falcon {

namespace {

// One block's result.
struct Result {
    std::string name;
    int quads = 0;          // quads inside the test cell
    int variedOn = 0;       // ... whose four corners are not all alike with Smooth Lighting on
    int variedOff = 0;      // ... and with it off (a flat quad has the same light and shade at every corner)
    bool aoModel = true;    // the block's model allows ambient occlusion (a cube always does)
};

bool varied(const MeshVertex* v) {
    for (int i = 1; i < 4; ++i) {
        if (v[i].sky != v[0].sky || v[i].block != v[0].block || v[i].shade != v[0].shade) return true;
    }
    return false;
}

}  // namespace

int runLightingAudit(const ResourceManager& resources, const std::filesystem::path& gameDataDir, const std::filesystem::path& cacheDir) {
    std::string error;
    auto registry = BlockRegistry::loadFromGameData(gameDataDir, cacheDir, error);
    if (!registry) {
        std::printf("[audit] cannot read the block data: %s\n", error.c_str());
        return 1;
    }
    installBlockRegistry(registry);
    BlockModels models(resources, *registry);

    // A flat world of stone with a test cell at (8, -60, 8) and two pillars of stone, two blocks tall, to its west and its south: they
    // reach into the layer above the cell, so the corners of its top face (and of anything inside it) have occluders to be shaded by.
    Level level;
    SectionBuilder floor;
    floor.fillLayers(0, 4, commonBlocks().stone);  // y -64 .. -61
    const SectionPtr floorSection = floor.build();
    for (int cz = -1; cz <= 1; ++cz) {
        for (int cx = -1; cx <= 1; ++cx) {
            auto chunk = std::make_shared<Chunk>();
            chunk->sections[0] = floorSection;
            level.putChunk({cx, cz}, std::move(chunk));
        }
    }
    constexpr int kX = 8, kY = -60, kZ = 8;
    for (const auto& p : {std::array<int, 2>{kX - 1, kZ}, std::array<int, 2>{kX, kZ + 1}, std::array<int, 2>{kX - 1, kZ + 1}}) {
        level.setBlock(p[0], kY, p[1], commonBlocks().stone);
        level.setBlock(p[0], kY + 1, p[1], commonBlocks().stone);
    }

    std::vector<Result> results;
    for (const BlockType& type : registry->blocks()) {
        const Block block = static_cast<Block>(type.defaultState);
        const StateInfo& info = registry->state(block);
        if (info.has(kStateAir) || info.has(kStateLiquid) || !info.has(kStateHasModel)) continue;
        level.setBlock(kX, kY, kZ, block);
        // The light of the middle chunk, from the blocks as they are now.
        if (Chunk* chunk = level.chunk({0, 0})) chunk->light = computeChunkLight(snapshotLightInput(level, 0, 0));

        Result result;
        result.name = type.name;
        const StateRender& render = models.render(block);
        if (render.kind == RenderKind::Model) {
            result.aoModel = false;
            for (const ModelPart& part : render.parts) {
                for (const ModelVariant& variant : part.variants) {
                    for (const ModelQuad& quad : *variant.quads) result.aoModel = result.aoModel || quad.ao;
                }
            }
        } else if (render.kind != RenderKind::Cube) {
            continue;
        }
        for (const bool smooth : {true, false}) {
            const SectionMesh mesh = meshSection(snapshotNeighborhood(level, 0, sectionOf(kY), 0), models, 0, kMinY + sectionOf(kY) * 16, 0, smooth, true);
            for (const std::vector<MeshVertex>* list : {&mesh.opaque, &mesh.cutout, &mesh.translucent}) {
                for (std::size_t q = 0; q + 3 < list->size(); q += 4) {
                    const MeshVertex* v = &(*list)[q];
                    // The quad belongs to the test cell when its middle is inside it (not on the cell's border: that is the pillars').
                    double cx = 0, cy = 0, cz = 0;
                    for (int i = 0; i < 4; ++i) {
                        cx += v[i].x / static_cast<double>(kMeshScale) / 4.0;
                        cy += v[i].y / static_cast<double>(kMeshScale) / 4.0;
                        cz += v[i].z / static_cast<double>(kMeshScale) / 4.0;
                    }
                    const double wy = cy + (kMinY + sectionOf(kY) * 16);
                    const double eps = 1.0e-3;
                    if (cx < kX + eps || cx > kX + 1 - eps || cz < kZ + eps || cz > kZ + 1 - eps || wy < kY + eps || wy > kY + 1 - eps) continue;
                    if (smooth) {
                        ++result.quads;
                        if (varied(v)) ++result.variedOn;
                    } else if (varied(v)) {
                        ++result.variedOff;
                    }
                }
            }
        }
        if (result.quads > 0) results.push_back(std::move(result));
    }

    // The report.
    int smooth = 0, flatByModel = 0;
    std::vector<const Result*> notSmooth, odd;
    for (const Result& r : results) {
        if (r.variedOff > 0) odd.push_back(&r);                       // flat lighting must never vary
        if (r.variedOn > 0) {
            ++smooth;
        } else if (!r.aoModel) {
            ++flatByModel;                                           // the game lights these flat too
        } else {
            notSmooth.push_back(&r);                                 // should vary, does not
        }
    }
    std::printf("[audit] %zu blocks meshed beside two pillars of stone\n", results.size());
    std::printf("[audit] smooth (corners differ): %d; flat because their model says \"ambientocclusion\": false: %d; NOT smooth though they should be: %zu\n",
                smooth, flatByModel, notSmooth.size());
    if (!odd.empty()) {
        std::printf("[audit] BUG: these vary with Smooth Lighting OFF:");
        for (const Result* r : odd) std::printf(" %s", r->name.c_str());
        std::printf("\n");
    }
    for (const Result* r : notSmooth) std::printf("[audit]   not smooth: %s (%d quads)\n", r->name.c_str(), r->quads);
    // The blocks lit flat because their model opts out, so they can be checked against what the game does.
    std::string flat;
    for (const Result& r : results) {
        if (r.variedOn == 0 && !r.aoModel) flat += " " + r.name;
    }
    std::printf("[audit] flat by their model:%s\n", flat.c_str());
    return 0;
}

}  // namespace falcon
