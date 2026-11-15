#include "gl_state.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <stdexcept>

namespace falcon::vulkan {

namespace {

constexpr GLenum kTexture2D = 0x0DE1;
constexpr GLenum kTexture2DArray = 0x8C1A;
constexpr GLenum kTexture3D = 0x806F;
constexpr GLenum kTextureCube = 0x8513;
constexpr GLenum kCubeFirstFace = 0x8515;
constexpr GLenum kRed = 0x1903;
constexpr GLenum kR8 = 0x8229;
constexpr GLenum kTextureMaxLevel = 0x813D;
constexpr GLenum kFramebufferIncompleteAttachment = 0x8CD6;
constexpr GLenum kMirroredRepeat = 0x8370;
constexpr GLenum kClampToBorder = 0x812D;

bool kindOfTarget(GLenum target, TexKind& kind) {
    if (target == kTexture2D) kind = Tex2D;
    else if (target == kTexture2DArray) kind = Tex2DArray;
    else if (target == kTexture3D) kind = Tex3D;
    else if (target == kTextureCube || (target >= kCubeFirstFace && target < kCubeFirstFace + 6)) kind = TexCube;
    else return false;
    return true;
}

GlTexture* boundTexture(GLenum target) {
    TexKind kind;
    if (!kindOfTarget(target, kind)) return nullptr;
    GlState& s = gls();
    return texturePtr(s.units[s.activeUnit].texture[kind]);
}

VkFormat vkFormatOf(GLenum internalFormat) {
    switch (internalFormat) {
        case kR8: return VK_FORMAT_R8_UNORM;
        case gl::RGBA16F: return VK_FORMAT_R16G16B16A16_SFLOAT;
        default: return VK_FORMAT_R8G8B8A8_UNORM;
    }
}

std::uint32_t bytesPerPixel(VkFormat format) {
    switch (format) {
        case VK_FORMAT_R8_UNORM: return 1;
        case VK_FORMAT_R16G16B16A16_SFLOAT: return 8;
        default: return 4;
    }
}

VkFilter filterOf(GLenum f) { return f == gl::NEAREST || f == 0x2700 || f == 0x2702 ? VK_FILTER_NEAREST : VK_FILTER_LINEAR; }

VkSamplerAddressMode wrapOf(GLenum w) {
    switch (w) {
        case gl::CLAMP_TO_EDGE: return VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        case kMirroredRepeat: return VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT;
        case kClampToBorder: return VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
        default: return VK_SAMPLER_ADDRESS_MODE_REPEAT;
    }
}

// A float as the 16 bits of a half float (what an RGBA16F texture holds).
std::uint16_t toHalf(float value) {
    std::uint32_t bits = 0;
    std::memcpy(&bits, &value, sizeof bits);
    const std::uint32_t sign = (bits >> 16) & 0x8000u;
    const std::int32_t exponent = static_cast<std::int32_t>((bits >> 23) & 0xFF) - 127 + 15;
    std::uint32_t mantissa = bits & 0x7FFFFFu;
    if (((bits >> 23) & 0xFF) == 0xFF) return static_cast<std::uint16_t>(sign | 0x7C00u | (mantissa != 0 ? 0x200u : 0u));  // infinity, not a number
    if (exponent >= 31) return static_cast<std::uint16_t>(sign | 0x7C00u);
    if (exponent <= 0) {
        if (exponent < -10) return static_cast<std::uint16_t>(sign);
        mantissa |= 0x800000u;
        const std::uint32_t shift = static_cast<std::uint32_t>(14 - exponent);
        return static_cast<std::uint16_t>(sign | ((mantissa + (1u << (shift - 1))) >> shift));
    }
    return static_cast<std::uint16_t>(sign | (static_cast<std::uint32_t>(exponent) << 10) | ((mantissa + 0x1000u) >> 13));
}

void transitionRange(VkCommandBuffer cb, VkImage image, VkImageLayout from, VkImageLayout to, std::uint32_t baseMip, std::uint32_t mips, std::uint32_t layers) {
    VkImageMemoryBarrier2 b{};
    b.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
    b.srcStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
    b.srcAccessMask = from == VK_IMAGE_LAYOUT_UNDEFINED ? 0 : VK_ACCESS_2_MEMORY_WRITE_BIT;
    b.dstStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
    b.dstAccessMask = VK_ACCESS_2_MEMORY_READ_BIT | VK_ACCESS_2_MEMORY_WRITE_BIT;
    b.oldLayout = from;
    b.newLayout = to;
    b.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    b.image = image;
    b.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, baseMip, mips, 0, layers};
    VkDependencyInfo dep{};
    dep.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
    dep.imageMemoryBarrierCount = 1;
    dep.pImageMemoryBarriers = &b;
    api::vkCmdPipelineBarrier2(cb, &dep);
}

void destroyTextureImage(GlTexture& t) {
    if (t.image.image == VK_NULL_HANDLE) return;
    VkImageView view = t.view;
    DeviceImage image = t.image;
    defer([view, image]() mutable {
        if (view != VK_NULL_HANDLE) api::vkDestroyImageView(core().device, view, nullptr);
        destroyImage(image);
    });
    t.image = DeviceImage{};
    t.view = VK_NULL_HANDLE;
    t.created = false;
    t.layout = VK_IMAGE_LAYOUT_UNDEFINED;
    t.dirty = false;
}

void markDirty(GLuint id, GlTexture& t) {
    if (t.dirty) return;
    t.dirty = true;
    gls().dirtyTextures.push_back(id);
}

GLuint idOf(const GlTexture& t) { return static_cast<GLuint>(&t - gls().textures.data()); }

// Puts pixels into a texture (levels, layers and offsets as the game gave them), converted to what the image holds.
void upload(GlTexture& t, int level, int x, int y, int z, int w, int h, int d, GLenum format, GLenum type, const void* data) {
    if (data == nullptr || w <= 0 || h <= 0 || d <= 0) return;
    if (!ensureTextureImage(t)) return;
    const std::uint32_t comps = format == kRed ? 1u : 4u;
    const std::uint32_t srcBytes = type == gl::FLOAT ? 4u : 1u;
    const std::uint32_t srcPixel = comps * srcBytes;
    const std::uint32_t dstPixel = bytesPerPixel(t.format);
    const std::size_t srcRowBytes = (static_cast<std::size_t>(w) * srcPixel + static_cast<std::size_t>(gls().unpackAlignment) - 1) / static_cast<std::size_t>(gls().unpackAlignment) *
                                    static_cast<std::size_t>(gls().unpackAlignment);
    const bool plain = (t.format == VK_FORMAT_R8G8B8A8_UNORM && format == gl::RGBA && type == gl::UNSIGNED_BYTE) || (t.format == VK_FORMAT_R8_UNORM && format == kRed && type == gl::UNSIGNED_BYTE);
    const bool toHalfFloat = t.format == VK_FORMAT_R16G16B16A16_SFLOAT && format == gl::RGBA && type == gl::FLOAT;
    if (!plain && !toHalfFloat) throw std::runtime_error("Vulkan: a texture upload of a kind the renderers do not make");

    beforeTransfer();
    const std::size_t dstRow = static_cast<std::size_t>(w) * dstPixel;
    const std::size_t total = dstRow * static_cast<std::size_t>(h) * static_cast<std::size_t>(d);
    const HostSlice staging = frame().arena.allocate(total, 16);
    auto* out = static_cast<unsigned char*>(staging.ptr);
    const auto* in = static_cast<const unsigned char*>(data);
    for (int slice = 0; slice < d; ++slice) {
        for (int row = 0; row < h; ++row) {
            const unsigned char* src = in + (static_cast<std::size_t>(slice) * static_cast<std::size_t>(h) + static_cast<std::size_t>(row)) * srcRowBytes;
            unsigned char* dst = out + (static_cast<std::size_t>(slice) * static_cast<std::size_t>(h) + static_cast<std::size_t>(row)) * dstRow;
            if (plain) {
                std::memcpy(dst, src, dstRow);
            } else {
                const auto* f = reinterpret_cast<const float*>(src);
                auto* hf = reinterpret_cast<std::uint16_t*>(dst);
                for (int i = 0; i < w * 4; ++i) hf[i] = toHalf(f[i]);
            }
        }
    }

    VkCommandBuffer cb = cmd();
    if (t.layout != VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL) {
        transitionRange(cb, t.image.image, t.layout, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 0, t.levels, t.layers);
        t.layout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    }
    VkBufferImageCopy region{};
    region.bufferOffset = staging.offset;
    region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    region.imageSubresource.mipLevel = static_cast<std::uint32_t>(level);
    if (t.kind == Tex3D) {
        region.imageSubresource.baseArrayLayer = 0;
        region.imageSubresource.layerCount = 1;
        region.imageOffset = {x, y, z};
        region.imageExtent = {static_cast<std::uint32_t>(w), static_cast<std::uint32_t>(h), static_cast<std::uint32_t>(d)};
    } else {
        region.imageSubresource.baseArrayLayer = static_cast<std::uint32_t>(z);
        region.imageSubresource.layerCount = static_cast<std::uint32_t>(d);
        region.imageOffset = {x, y, 0};
        region.imageExtent = {static_cast<std::uint32_t>(w), static_cast<std::uint32_t>(h), 1};
    }
    api::vkCmdCopyBufferToImage(cb, staging.buffer, t.image.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
    t.written = true;
    markDirty(idOf(t), t);
}

// The common part of glTexImage2D and glTexImage3D.
void specify(GLenum target, GLint level, GLint internalFormat, GLsizei w, GLsizei h, GLsizei depth, GLenum format, GLenum type, const void* data) {
    TexKind kind;
    GlTexture* t = boundTexture(target);
    if (t == nullptr || !kindOfTarget(target, kind)) return;
    int face = 0;
    int layers = depth;
    if (kind == TexCube) {
        face = target >= kCubeFirstFace ? static_cast<int>(target - kCubeFirstFace) : 0;
        layers = 6;
    }
    t->kind = kind;
    t->kindKnown = true;
    const GLenum newFormat = static_cast<GLenum>(internalFormat);
    if (level == 0) {
        const bool changed = t->created && (t->width != w || t->height != h || t->depth != layers || t->internalFormat != newFormat);
        if (changed) destroyTextureImage(*t);
        if (!t->created) t->levelsDefined = 0;
        t->width = w;
        t->height = h;
        t->depth = layers;
        t->internalFormat = newFormat;
        t->format = vkFormatOf(newFormat);
    }
    t->levelsDefined = std::max(t->levelsDefined, static_cast<int>(level) + 1);
    if (data != nullptr) {
        upload(*t, level, 0, 0, kind == TexCube ? face : 0, w, h, kind == Tex2D || kind == TexCube ? 1 : depth, format, type, data);
    } else if (kind == Tex2D && level == 0) {
        t->attachable = true;  // (made empty: the game draws into it)
    }
}

}  // namespace

GlTexture* texturePtr(GLuint id) {
    auto& all = gls().textures;
    return id != 0 && id < all.size() && all[id].exists ? &all[id] : nullptr;
}

bool ensureTextureImage(GlTexture& t) {
    if (t.created) return true;
    if (!t.exists || !t.kindKnown || t.width <= 0 || t.height <= 0) return false;
    int maxDim = std::max(t.width, t.height);
    if (t.kind == Tex3D) maxDim = std::max(maxDim, t.depth);
    int full = 1;
    while ((maxDim >> full) > 0) ++full;
    t.levels = static_cast<std::uint32_t>(std::clamp(t.levelsDefined, 1, std::min(full, std::max(t.maxLevel, 0) + 1)));
    t.layers = t.kind == Tex2DArray ? static_cast<std::uint32_t>(std::max(t.depth, 1)) : (t.kind == TexCube ? 6u : 1u);

    VkImageCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    info.imageType = t.kind == Tex3D ? VK_IMAGE_TYPE_3D : VK_IMAGE_TYPE_2D;
    info.format = t.format;
    info.extent = {static_cast<std::uint32_t>(t.width), static_cast<std::uint32_t>(t.height), t.kind == Tex3D ? static_cast<std::uint32_t>(std::max(t.depth, 1)) : 1u};
    info.mipLevels = t.levels;
    info.arrayLayers = t.layers;
    info.samples = VK_SAMPLE_COUNT_1_BIT;
    info.tiling = VK_IMAGE_TILING_OPTIMAL;
    info.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    if (t.attachable && t.format == VK_FORMAT_R8G8B8A8_UNORM) info.usage |= VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    if (t.kind == TexCube) info.flags = VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT;
    info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    if (!createImage(info, t.image)) throw std::runtime_error("Vulkan: no memory on the graphics card for a texture of " + std::to_string(t.width) + "x" + std::to_string(t.height));

    VkImageViewCreateInfo vi{};
    vi.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    vi.image = t.image.image;
    vi.viewType = t.kind == Tex3D ? VK_IMAGE_VIEW_TYPE_3D : (t.kind == Tex2DArray ? VK_IMAGE_VIEW_TYPE_2D_ARRAY : (t.kind == TexCube ? VK_IMAGE_VIEW_TYPE_CUBE : VK_IMAGE_VIEW_TYPE_2D));
    vi.format = t.format;
    vi.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, t.levels, 0, t.layers};
    if (api::vkCreateImageView(core().device, &vi, nullptr, &t.view) != VK_SUCCESS) throw std::runtime_error("Vulkan: vkCreateImageView failed");
    t.layout = VK_IMAGE_LAYOUT_UNDEFINED;
    t.created = true;
    return true;
}

void samplerBinding(int unit, int kind, VkImageView& view, VkSampler& sampler) {
    GlState& s = gls();
    GlTexture* t = texturePtr(s.units[unit].texture[kind]);
    if (t == nullptr || !ensureTextureImage(*t)) t = texturePtr(s.dummyTextures[kind]);
    if (t == nullptr || !ensureTextureImage(*t)) {
        view = VK_NULL_HANDLE;
        sampler = VK_NULL_HANDLE;
        return;
    }
    // (A texture nothing has written yet is made readable so the draw is valid; it holds whatever the memory held.)
    if (t->layout == VK_IMAGE_LAYOUT_UNDEFINED) {
        endRendering();
        transitionRange(cmd(), t->image.image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, 0, t->levels, t->layers);
        t->layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    }
    view = t->view;
    const bool mipmapped = t->minFilter >= 0x2700 && t->minFilter <= 0x2703;
    const VkFilter minFilter = filterOf(t->minFilter);
    const VkSamplerMipmapMode mipMode = (t->minFilter == 0x2702 || t->minFilter == 0x2703) ? VK_SAMPLER_MIPMAP_MODE_LINEAR : VK_SAMPLER_MIPMAP_MODE_NEAREST;
    const float maxLod = mipmapped ? static_cast<float>(std::min<std::uint32_t>(static_cast<std::uint32_t>(std::max(t->maxLevel, 0)), t->levels - 1)) : 0.0f;
    sampler = samplerFor(minFilter, filterOf(t->magFilter), mipMode, wrapOf(t->wrapS), wrapOf(t->wrapT), wrapOf(t->wrapR), maxLod);
}

bool targetOf(GLuint framebuffer, Target& out) {
    out = Target{};
    if (framebuffer == 0) {
        if (backbuffer().color.image == VK_NULL_HANDLE) ensureBackbuffer();
        Backbuffer& bb = backbuffer();
        out.isBackbuffer = true;
        out.colorImage = bb.color.image;
        out.colorView = bb.colorView;
        out.colorFormat = VK_FORMAT_R8G8B8A8_UNORM;
        out.depthImage = bb.depth.image;
        out.depthView = bb.depthView;
        out.depthFormat = core().depthFormat;
        out.width = bb.width;
        out.height = bb.height;
        out.colorLayout = &bb.colorLayout;
        out.depthLayout = &bb.depthLayout;
        out.colorValid = &bb.colorValid;
        out.depthValid = &bb.depthValid;
        out.clear = &bb.clear;
        return true;
    }
    GlState& s = gls();
    if (framebuffer >= s.framebuffers.size() || !s.framebuffers[framebuffer].exists) return false;
    GlTexture* t = texturePtr(s.framebuffers[framebuffer].colorTexture);
    if (t == nullptr || !t->attachable || !ensureTextureImage(*t)) return false;
    out.isBackbuffer = false;
    out.texture = t;
    out.colorImage = t->image.image;
    out.colorView = t->view;
    out.colorFormat = t->format;
    out.width = static_cast<std::uint32_t>(t->width);
    out.height = static_cast<std::uint32_t>(t->height);
    out.colorLayout = &t->layout;
    out.colorValid = &t->targetValid;
    out.clear = &t->clear;
    return true;
}

void flushTransfers() {
    GlState& s = gls();
    for (const GLuint id : s.dirtyTextures) {
        GlTexture* t = texturePtr(id);
        if (t == nullptr) continue;
        t->dirty = false;
        if (t->created && t->layout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL) {
            transitionRange(cmd(), t->image.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, 0, t->levels, t->layers);
            t->layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        }
    }
    s.dirtyTextures.clear();
    if (s.bufferBarrierPending) {
        fullBarrier(cmd());
        s.bufferBarrierPending = false;
    }
}

void createDummyTextures() {
    GlState& s = gls();
    const unsigned char black[4] = {0, 0, 0, 255};
    const GLuint savedBinding[4] = {s.units[0].texture[0], s.units[0].texture[1], s.units[0].texture[2], s.units[0].texture[3]};
    const int savedUnit = s.activeUnit;
    s.activeUnit = 0;
    const GLenum targets[4] = {kTexture2D, kTexture2DArray, kTexture3D, kTextureCube};
    for (int kind = 0; kind < 4; ++kind) {
        glemu::GenTextures(1, &s.dummyTextures[kind]);
        glemu::BindTexture(targets[kind], s.dummyTextures[kind]);
        if (kind == TexCube) {
            for (GLenum face = 0; face < 6; ++face) glemu::TexImage2D(kCubeFirstFace + face, 0, gl::RGBA8, 1, 1, 0, gl::RGBA, gl::UNSIGNED_BYTE, black);
        } else if (kind == Tex2D) {
            glemu::TexImage2D(targets[kind], 0, gl::RGBA8, 1, 1, 0, gl::RGBA, gl::UNSIGNED_BYTE, black);
        } else {
            glemu::TexImage3D(targets[kind], 0, gl::RGBA8, 1, 1, 1, 0, gl::RGBA, gl::UNSIGNED_BYTE, black);
        }
        glemu::TexParameteri(targets[kind], gl::TEXTURE_MIN_FILTER, static_cast<GLint>(gl::NEAREST));
        glemu::TexParameteri(targets[kind], gl::TEXTURE_MAG_FILTER, static_cast<GLint>(gl::NEAREST));
    }
    for (int kind = 0; kind < 4; ++kind) s.units[0].texture[kind] = savedBinding[kind];
    s.activeUnit = savedUnit;
}

void destroyTextureObjects() {
    Core& c = core();
    for (GlTexture& t : gls().textures) {
        if (t.view != VK_NULL_HANDLE) api::vkDestroyImageView(c.device, t.view, nullptr);
        destroyImage(t.image);
        t.view = VK_NULL_HANDLE;
    }
}

namespace glemu {

void GenTextures(GLsizei n, GLuint* ids) {
    auto& all = gls().textures;
    for (GLsizei i = 0; i < n; ++i) {
        all.emplace_back();
        all.back().exists = true;
        ids[i] = static_cast<GLuint>(all.size() - 1);
    }
}

void DeleteTextures(GLsizei n, const GLuint* ids) {
    GlState& s = gls();
    for (GLsizei i = 0; i < n; ++i) {
        GlTexture* t = texturePtr(ids[i]);
        if (t == nullptr) continue;
        if (s.rendering && s.renderTarget.texture == t) endRendering();
        destroyTextureImage(*t);
        for (GlFramebuffer& fb : s.framebuffers) {
            if (fb.colorTexture == ids[i]) fb.colorTexture = 0;
        }
        for (Unit& u : s.units) {
            for (GLuint& bound : u.texture) {
                if (bound == ids[i]) bound = 0;
            }
        }
        *t = GlTexture{};
    }
}

void BindTexture(GLenum target, GLuint id) {
    TexKind kind;
    if (!kindOfTarget(target, kind)) return;
    GlState& s = gls();
    if (id != 0 && texturePtr(id) == nullptr) return;
    s.units[s.activeUnit].texture[kind] = id;
    if (GlTexture* t = texturePtr(id)) {
        if (!t->kindKnown) {
            t->kind = kind;
            t->kindKnown = true;
        }
    }
}

void ActiveTexture(GLenum unit) {
    const int index = static_cast<int>(unit) - static_cast<int>(gl::TEXTURE0);
    if (index >= 0 && index < 16) gls().activeUnit = index;
}

void TexImage2D(GLenum target, GLint level, GLint internalFormat, GLsizei width, GLsizei height, GLint, GLenum format, GLenum type, const void* data) {
    specify(target, level, internalFormat, width, height, 1, format, type, data);
}

void TexImage3D(GLenum target, GLint level, GLint internalFormat, GLsizei width, GLsizei height, GLsizei depth, GLint, GLenum format, GLenum type, const void* data) {
    specify(target, level, internalFormat, width, height, depth, format, type, data);
}

void TexSubImage2D(GLenum target, GLint level, GLint x, GLint y, GLsizei width, GLsizei height, GLenum format, GLenum type, const void* data) {
    GlTexture* t = boundTexture(target);
    if (t == nullptr) return;
    const int face = t->kind == TexCube && target >= kCubeFirstFace ? static_cast<int>(target - kCubeFirstFace) : 0;
    upload(*t, level, x, y, face, width, height, 1, format, type, data);
}

void TexSubImage3D(GLenum target, GLint level, GLint x, GLint y, GLint z, GLsizei width, GLsizei height, GLsizei depth, GLenum format, GLenum type, const void* data) {
    GlTexture* t = boundTexture(target);
    if (t == nullptr) return;
    upload(*t, level, x, y, z, width, height, depth, format, type, data);
}

void TexParameteri(GLenum target, GLenum name, GLint value) {
    GlTexture* t = boundTexture(target);
    if (t == nullptr) return;
    const auto v = static_cast<GLenum>(value);
    switch (name) {
        case gl::TEXTURE_MIN_FILTER: t->minFilter = v; break;
        case gl::TEXTURE_MAG_FILTER: t->magFilter = v; break;
        case gl::TEXTURE_WRAP_S: t->wrapS = v; break;
        case gl::TEXTURE_WRAP_T: t->wrapT = v; break;
        case gl::TEXTURE_WRAP_R: t->wrapR = v; break;
        case kTextureMaxLevel: t->maxLevel = value; break;
        default: break;
    }
}

void PixelStorei(GLenum name, GLint value) {
    if (name == gl::UNPACK_ALIGNMENT) gls().unpackAlignment = value;
    else if (name == gl::PACK_ALIGNMENT) gls().packAlignment = value;
}

// The levels below the first are made from it, each half the size of the one before (a box filter: what the blit's linear filter does for exact halves).
void GenerateMipmap(GLenum target) {
    GlTexture* t = boundTexture(target);
    if (t == nullptr || !ensureTextureImage(*t) || t->levels < 2) return;
    beforeTransfer();
    VkCommandBuffer cb = cmd();
    if (t->layout != VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL) {
        transitionRange(cb, t->image.image, t->layout, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 0, t->levels, t->layers);
        t->layout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    }
    std::int32_t w = t->width, h = t->height, d = t->kind == Tex3D ? t->depth : 1;
    for (std::uint32_t level = 1; level < t->levels; ++level) {
        transitionRange(cb, t->image.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, level - 1, 1, t->layers);
        const std::int32_t nw = std::max(1, w / 2), nh = std::max(1, h / 2), nd = std::max(1, d / 2);
        VkImageBlit blit{};
        blit.srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, level - 1, 0, t->layers};
        blit.srcOffsets[1] = {w, h, d};
        blit.dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, level, 0, t->layers};
        blit.dstOffsets[1] = {nw, nh, nd};
        api::vkCmdBlitImage(cb, t->image.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, t->image.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &blit, VK_FILTER_LINEAR);
        w = nw;
        h = nh;
        d = nd;
    }
    transitionRange(cb, t->image.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, 0, t->levels - 1, t->layers);
    transitionRange(cb, t->image.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, t->levels - 1, 1, t->layers);
    t->layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    t->written = true;
}

void GenFramebuffers(GLsizei n, GLuint* ids) {
    auto& all = gls().framebuffers;
    for (GLsizei i = 0; i < n; ++i) {
        all.emplace_back();
        all.back().exists = true;
        ids[i] = static_cast<GLuint>(all.size() - 1);
    }
}

void DeleteFramebuffers(GLsizei n, const GLuint* ids) {
    GlState& s = gls();
    for (GLsizei i = 0; i < n; ++i) {
        if (ids[i] != 0 && ids[i] < s.framebuffers.size()) s.framebuffers[ids[i]] = GlFramebuffer{};
        if (s.framebuffer == ids[i]) s.framebuffer = 0;
    }
}

void BindFramebuffer(GLenum, GLuint id) {
    GlState& s = gls();
    if (id != 0 && (id >= s.framebuffers.size() || !s.framebuffers[id].exists)) return;
    if (s.framebuffer != id) endRendering();
    s.framebuffer = id;
    s.viewportKnown = false;
    s.scissorKnown = false;
}

void FramebufferTexture2D(GLenum, GLenum attachment, GLenum, GLuint texture, GLint) {
    GlState& s = gls();
    if (s.framebuffer == 0 || attachment != gl::COLOR_ATTACHMENT0) return;
    s.framebuffers[s.framebuffer].colorTexture = texture;
    if (GlTexture* t = texturePtr(texture)) {
        t->attachable = true;
        if (t->created && (t->image.image != VK_NULL_HANDLE)) {
            // (made before it was known to be a target: it was given pixels, so the picture cannot be drawn into)
        }
        (void)ensureTextureImage(*t);
    }
}

GLenum CheckFramebufferStatus(GLenum) {
    GlState& s = gls();
    if (s.framebuffer == 0) return gl::FRAMEBUFFER_COMPLETE;
    Target t;
    return targetOf(s.framebuffer, t) ? gl::FRAMEBUFFER_COMPLETE : kFramebufferIncompleteAttachment;
}

}  // namespace glemu

}  // namespace falcon::vulkan
