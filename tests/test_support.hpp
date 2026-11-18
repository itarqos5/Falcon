#pragma once

// Shared by the files of the client tests (test_client*.cpp): the includes, CHECK, and the helpers more than one file uses.
#include "falcon/assets/asset_index.hpp"
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>  // for the loopback fake server of the ping test (must come before other Windows headers)
#include <ws2tcpip.h>
#undef near  // <windef.h> defines these two as empty macros; the helper below is called near()
#undef far
#endif

#include "falcon/assets/nbt.hpp"
#include "falcon/assets/resource_manager.hpp"
#include "falcon/assets/zip_archive.hpp"
#include "falcon/packs/pack_repository.hpp"
#include "falcon/packs/pack_selection_model.hpp"
#include "falcon/text/language.hpp"
#include "falcon/ui/scroll_list.hpp"
#include "falcon/audio/audio_engine.hpp"
#include "falcon/audio/music_manager.hpp"
#include "falcon/audio/sound_registry.hpp"
#include "falcon/audio/sound_system.hpp"
#include "falcon/assets/resource_location.hpp"
#include "falcon/client.hpp"
#include "falcon/game/edition_badge.hpp"
#include "falcon/game/game_options.hpp"
#include "falcon/game/survival_hud.hpp"
#include "falcon/game/world_sounds.hpp"
#include "falcon/world/item_entity.hpp"
#include "falcon/world/item_registry.hpp"
#include "falcon/world/world_save.hpp"
#include "falcon/platform/ownership.hpp"
#include "falcon/platform/system_info.hpp"
#include "falcon/world/chunk_cache.hpp"
#include "falcon/game/gamemode_switcher_screen.hpp"
#include "falcon/ui/gui_scale.hpp"
#include "falcon/ui/layout.hpp"
#include "falcon/ui/list_screen.hpp"
#include "falcon/game/language_screen.hpp"
#include "falcon/game/now_playing_toast.hpp"
#include "falcon/game/options_screen.hpp"
#include "falcon/game/create_world_screen.hpp"
#include "falcon/game/join_multiplayer_screen.hpp"
#include "falcon/game/key_binds_screen.hpp"
#include "falcon/game/key_mappings.hpp"
#include "falcon/platform/key_names.hpp"
#include "falcon/game/select_world_screen.hpp"
#include "falcon/game/sound_options_screen.hpp"
#include "falcon/game/splash_manager.hpp"
#include "falcon/game/title_screen.hpp"
#include "falcon/ui/widgets.hpp"
#include "falcon/util/math.hpp"
#include "falcon/util/options_file.hpp"
#include "falcon/util/text_util.hpp"
#include "falcon/world/level_summary.hpp"
#include "falcon/net/server_pinger.hpp"
#include "falcon/text/styled_text.hpp"
#include "falcon/util/base64.hpp"
#include "falcon/util/server_address.hpp"
#include "falcon/game/in_game_screen.hpp"
#include "falcon/gfx/mipmaps.hpp"
#include "falcon/render/camera.hpp"
#include "falcon/render/world_renderer.hpp"
#include "falcon/world/block_outlines.hpp"
#include "falcon/world/fluids.hpp"
#include "falcon/world/water_fog.hpp"
#include "falcon/util/priority_pool.hpp"
#include "falcon/world/level_info.hpp"
#include "falcon/assets/asset_locator.hpp"
#include "falcon/world/block_registry.hpp"
#include "falcon/world/block_render.hpp"
#include "falcon/world/light.hpp"
#include "falcon/world/mesher.hpp"
#include "falcon/world/player.hpp"
#include "falcon/world/raycast.hpp"
#include "falcon/world/new_world.hpp"
#include "falcon/world/terrain.hpp"
#include "falcon/world/server_list.hpp"

#include <GLFW/glfw3.h>

#include <algorithm>
#include <array>
#include <map>
#include <set>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <memory>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <random>
#include <thread>
#include <type_traits>
#include <vector>

extern int g_failures;  // (counted by CHECK; defined in test_client.cpp)

#define CHECK(expr)                                                                        \
    do {                                                                                   \
        if (!(expr)) {                                                                     \
            std::cerr << __FILE__ << ':' << __LINE__ << ": CHECK failed: " << #expr << '\n'; \
            ++g_failures;                                                                  \
        }                                                                                  \
    } while (false)

inline bool near(float a, float b, float eps = 1e-4f) { return std::fabs(a - b) <= eps; }

// A stand-in for the baked block models: opaque cubes with a picture per state (grass has its own top and side),
// water as a fluid, everything else invisible. Lets the mesher be tested without the game's files.
class FakeModels : public falcon::BlockRenderProvider {
public:
    static constexpr int kGrassTop = 200, kGrassSide = 201, kGrassBottom = 202, kWaterStill = 300, kWaterFlow = 301;
    const falcon::StateRender& render(falcon::Block b) const override {
        const auto& registry = falcon::blockRegistry();
        auto it = cache_.find(falcon::stateId(b));
        if (it != cache_.end()) return it->second;
        falcon::StateRender s;
        const falcon::StateInfo& info = registry.state(b);
        if (info.has(falcon::kStateLiquid)) {
            s.kind = falcon::RenderKind::Fluid;
            s.fluidStill = kWaterStill;
            s.fluidFlow = kWaterFlow;
            s.fluidTint = 0;
            s.type = falcon::RenderType::Translucent;
        } else if (info.has(falcon::kStateOpaqueCube)) {
            s.kind = falcon::RenderKind::Cube;
            s.faceLayer.fill(static_cast<std::uint16_t>(falcon::stateId(b)));
            s.faceTint.fill(-1);
            if (registry.type(b).name == "grass_block") {
                s.faceLayer = {kGrassSide, kGrassSide, kGrassTop, kGrassBottom, kGrassSide, kGrassSide};
            }
        }
        return cache_.emplace(falcon::stateId(b), std::move(s)).first->second;
    }

private:
    mutable std::unordered_map<std::uint16_t, falcon::StateRender> cache_;
};

// Helpers defined in one file and used in others.
struct ZipFile {
    std::string name;
    std::string data;
    bool deflateStored = false;  // method 8 with a single "stored" deflate block
};
std::vector<unsigned char> makeZip(const std::vector<ZipFile>& files);
void writeBytes(const std::filesystem::path& file, const std::vector<unsigned char>& bytes);
void writeText(const std::filesystem::path& file, const std::string& text);
void nbtBE(std::vector<unsigned char>& out, unsigned long long v, int bytes);
void nbtName(std::vector<unsigned char>& out, const std::string& s);
void nbtHead(std::vector<unsigned char>& out, int type, const std::string& name);
void makeWorld(const std::filesystem::path& saves, const std::string& id, const std::vector<unsigned char>& dataFields, bool lock = true);
std::shared_ptr<falcon::PcmSound> makeSound(int rate, int channels, std::size_t frames, float value);
falcon::Level makeFluidFloor();
falcon::Level makeFlatLevel();
falcon::Level makeFloorLevel(int radius = 3);
falcon::PlayerInput keysForward();
falcon::Block blockNamed(const char* name);

// A player on a level that is ticked, and the constants of the game's physics.
// A player and the level it stands in, ticked one game tick at a time.
struct PhysicsRun {
    falcon::Level& level;
    falcon::Player p;
    explicit PhysicsRun(falcon::Level& l, double x = 0.5, double y = -60.0, double z = 0.5, float yaw = 0.0f) : level(l) {
        p.setPosition(x, y, z);
        p.yaw = p.yawO = yaw;
    }
    void tick(const falcon::PlayerInput& in = {}) { p.tick(in, level); }
    void ticks(int n, const falcon::PlayerInput& in = {}) {
        for (int i = 0; i < n; ++i) tick(in);
    }
    // Ticks with no keys until the player stands on the ground.
    void settle() { ticks(3); }
    // How far the last tick moved the player horizontally.
    double lastStep(const falcon::PlayerInput& in) {
        const double x0 = p.x, z0 = p.z;
        tick(in);
        return std::hypot(p.x - x0, p.z - z0);
    }
};

inline bool closeTo(double a, double b, double eps) { return std::abs(a - b) <= eps; }

// The steady distance a tick moves when the push per tick is `accel` and the drag is `drag`: a / (1 - drag).
inline double steady(double accel, double drag) { return accel / (1.0 - drag); }

// The game's float constants as doubles, the way they enter the formulas.
inline const double kInputF = static_cast<double>(0.98f);                       // modifyInput: the keys' push at 0.98f
inline const double kWalkSpeedF = static_cast<double>(0.1f);                    // movement_speed 0.10000000149011612
inline const double kSprintSpeedF = static_cast<double>(static_cast<float>(0.10000000149011612 * (1.0 + 0.30000001192092896)));  // +30%
inline const double kGroundDragF = static_cast<double>(0.6f * 0.91f);           // friction 0.6f * 0.91f (a float product)

// The tests themselves.
void test_client_identity();
void test_options();
void test_stream_radius_covers_ready_area();
void test_mipmaps();
void test_fluids();
void test_water_fog();
void test_resource_location();
void test_gui_scale();
void test_title_layout();
void test_survival_hud();
void test_system_info();
void test_world_sounds();
void test_item_entities();
void test_world_save();
void test_ownership_answers();
void test_options_layout();
void test_fov_mapping();
void test_format_language_string();
void test_slider();
void test_audio_mixer();
void test_sound_registry();
void test_sound_system_and_music();
void test_now_playing_toast();
void test_zip_archive();
void test_mod_metadata();
void test_mods_folder();
void test_optimization_mods();
void test_nbt();
void test_options_file();
void test_ui_library_layout();
void test_languages();
void test_graphics_api_setting();
void test_gamemode_switcher_logic();
void test_chunk_cache();
void test_game_options_round_trip();
void test_select_world_layout();
void test_level_summary();
void test_resource_layers();
void test_pack_repository_and_model();
void test_scroll_list();
void test_cycle_button();
void test_audio_devices();
void test_splash_dates();
void test_widget_clicks();
void test_asset_index();
void test_edition_badge();
void test_text_util();
void test_math();
void test_nbt_writer();
void test_create_world_layout();
void test_new_world();
void test_base64();
void test_server_address();
void test_styled_text();
void test_server_list();
void test_server_ping();
void test_multiplayer_layout();
void test_key_mappings();
void test_key_binds_layout();
void test_block_registry();
void test_world_storage();
void test_terrain();
void test_mesher();
void test_player_input_and_walking();
void test_player_jump_and_fall();
void test_player_flying();
void test_player_sneak_edge_and_poses();
void test_player_stepping();
void test_player_fluids();
void test_boat();
void test_text_edit();
void test_remote_players();
void test_player_current();
void test_player_real_blocks();
void test_player_physics();
void test_raycast();
void test_carried_light();
void test_camera_and_frustum();
void test_priority_pool_and_level_info();
void test_pause_layout();
void test_light();
