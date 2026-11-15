#include "falcon/render/world_renderer.hpp"
#include "world_renderer_internal.hpp"

#include "falcon/world/block_outlines.hpp"
#include "falcon/world/block_position.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>

// WorldRenderer: changing blocks, the light at a place, and what the generator says about the spawn.

namespace falcon {

void WorldRenderer::meshNow(const SectionKey& key) {
    const ChunkPos pos{key.x, key.z};
    if (key.y < 0 || key.y >= kSectionCount || level_.chunk(pos) == nullptr || !neighborsLit(pos)) return;
    MeshResult result{key, stampOf(pos), {}, 0, 0, 0, ++meshGen_[key]};
    if (level_.chunk(pos)->sections[static_cast<std::size_t>(key.y)]) {
        const bool lod = leafLodFor(key);
        SectionMesh mesh = meshSection(snapshotNeighborhood(level_, key.x, key.y, key.z), models_, key.x * 16, kMinY + key.y * 16, key.z * 16, smoothLighting_, cutoutLeaves_, lod);
        result.leafInterior = mesh.leafInterior;
        result.visibility = mesh.visibility;
        result.lodFar = lod;
        packMesh(result, std::move(mesh));
    }
    meshRequested_.insert(key);
    uploadMesh(std::move(result));
}

bool WorldRenderer::setBlock(int x, int y, int z, Block block) {
    if (!level_.setBlock(x, y, z, block)) return false;
    if (edits_) edits_->set(x, y, z, block);
    ++blockEdits_;  // the carried light may now reach round a corner, or stop at a wall
    const int lx = x & 15;
    const int lz = z & 15;
    const int ly = (y - kMinY) & 15;
    const int cx = chunkOf(x);
    const int cz = chunkOf(z);
    const int sy = sectionOf(y);
    // The edited section and, when the block is on a border, the section next to it: their faces change too.
    std::vector<SectionKey> touched = {{cx, cz, sy}};
    if (lx == 0) touched.push_back({cx - 1, cz, sy});
    if (lx == 15) touched.push_back({cx + 1, cz, sy});
    if (lz == 0) touched.push_back({cx, cz - 1, sy});
    if (lz == 15) touched.push_back({cx, cz + 1, sy});
    if (ly == 0) touched.push_back({cx, cz, sy - 1});
    if (ly == 15) touched.push_back({cx, cz, sy + 1});
    for (const SectionKey& k : touched) ++editStamps_[{k.x, k.z}];  // any mesh still being made from older data is stale
    if (batchDepth_ > 0) {  // (see beginBatch: the meshes and the light wait for the end of the batch)
        for (const SectionKey& k : touched) {
            if (std::find(batchTouched_.begin(), batchTouched_.end(), k) == batchTouched_.end()) batchTouched_.push_back(k);
        }
        for (int dz = -1; dz <= 1; ++dz) {
            for (int dx = -1; dx <= 1; ++dx) batchLight_.insert({cx + dx, cz + dz});
        }
        return true;
    }
    for (const SectionKey& k : touched) meshNow(k);
    // The light changes too (a torch, a roof, a hole in the ground): redo it for the chunk and the ones around it.
    for (int dz = -1; dz <= 1; ++dz) {
        for (int dx = -1; dx <= 1; ++dx) requestLight({cx + dx, cz + dz}, true);
    }
    return true;
}

void WorldRenderer::endBatch() {
    if (batchDepth_ > 0 && --batchDepth_ > 0) return;
    batchDepth_ = 0;
    const std::vector<SectionKey> touched = std::move(batchTouched_);
    batchTouched_.clear();
    for (const SectionKey& k : touched) meshNow(k);
    for (const ChunkPos& c : batchLight_) requestLight(c, true);
    batchLight_.clear();
}

TerrainGenerator::Spawn WorldRenderer::findSpawn() const {
    if (overworld_) {
        const worldgen::OverworldGenerator::Spawn s = overworld_->findSpawn();
        return {s.x, s.y, s.z};
    }
    return generator_.findSpawn();
}

int WorldRenderer::surfaceHeight(int x, int z) const { return overworld_ ? overworld_->surfaceHeightAt(x, z) : generator_.surfaceHeight(x, z); }

void WorldRenderer::lightAt(int x, int y, int z, int& sky, int& block) const {
    sky = 15;
    block = 0;
    const Chunk* chunk = level_.chunk({chunkOf(x), chunkOf(z)});
    if (chunk == nullptr || !chunk->light) return;
    sky = chunk->light->skyAt(x & 15, y, z & 15);
    block = chunk->light->blockAt(x & 15, y, z & 15);
}

void WorldRenderer::lightAtWithCarried(int x, int y, int z, int& sky, int& block) const {
    lightAt(x, y, z, sky, block);
    if (heldLightLevel_ <= 0) return;
    const int half = kHeldLightSize / 2;  // the volume's corner is `half` blocks off the block it shines from (see setCarriedLight)
    const int cx = x - (heldLightX_ - half), cy = y - (heldLightY_ - half), cz = z - (heldLightZ_ - half);
    if (cx < 0 || cy < 0 || cz < 0 || cx >= kHeldLightSize || cy >= kHeldLightSize || cz >= kHeldLightSize) return;
    const std::size_t index = static_cast<std::size_t>(cx + kHeldLightSize * (cy + kHeldLightSize * cz));
    if (index < heldLightVolume_.size()) block = std::max(block, static_cast<int>(heldLightVolume_[index]) / 17);
}

}  // namespace falcon
