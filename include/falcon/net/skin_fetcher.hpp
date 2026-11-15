#pragma once

#include "falcon/assets/image.hpp"

#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace falcon::net {

// Where a player's skin is and which model it is made for: what the "textures" property of a profile says.
struct SkinInfo {
    std::string hash;  // the picture is textures.minecraft.net/texture/<hash>
    bool slim = false; // arms of three pixels
};

// The skin that a "textures" property (base64 of JSON: {"textures":{"SKIN":{"url":...,"metadata":{"model":"slim"}}}}) names. Only a picture on textures.minecraft.net is
// accepted (the address in the property is not followed anywhere else). Empty for a property that names none.
[[nodiscard]] std::optional<SkinInfo> parseTexturesProperty(const std::string& base64Value);

// A downloaded skin as the game makes it ready (HttpTexture.processLegacySkin): an old 64x32 skin becomes 64x64 (the limbs are copied mirrored to the lower half, and a hat
// layer that is all opaque is dropped), and the base layers lose their transparency. Empty for a picture that is not 64x32 or 64x64.
[[nodiscard]] Image prepareSkin(Image decoded);

// Downloads skins on a thread of its own, one at a time, and hands them back once they are ready. Nothing here blocks the game.
class SkinFetcher {
public:
    SkinFetcher();
    ~SkinFetcher();
    SkinFetcher(const SkinFetcher&) = delete;
    SkinFetcher& operator=(const SkinFetcher&) = delete;

    // The picture of this skin, unless it was asked for already.
    void request(const SkinInfo& info);
    // The skin of an account (its id with or without dashes): the session server is asked for the profile, then the picture. What anyone sees of an account is public.
    void requestProfile(const std::string& id);

    struct Done {
        std::string hash;
        bool slim = false;
        Image image;
    };
    // The skins that are ready since the last call.
    [[nodiscard]] std::vector<Done> takeFinished();
    // The skin an account was found to have (once its profile has been read).
    [[nodiscard]] std::optional<SkinInfo> profileSkin(const std::string& id) const;

private:
    struct State;
    static void fetchPicture(const std::shared_ptr<State>& state, const SkinInfo& info);  // (on the worker thread) the picture, decoded and made ready
    std::shared_ptr<State> state_;
};

}  // namespace falcon::net
