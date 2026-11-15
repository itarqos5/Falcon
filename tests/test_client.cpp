// Minimal dependency-free test runner for the logic that needs no OpenGL context (the tests are in test_client_*.cpp).
// Swap for Catch2/GoogleTest when the suite outgrows this.
#include "test_support.hpp"

int g_failures = 0;

int main() {
    falcon::installBlockRegistry(falcon::BlockRegistry::makeMinimal());  // the tests need no game files for blocks
    test_client_identity();
    test_options();
    test_stream_radius_covers_ready_area();
    test_mipmaps();
    test_water_fog();
    test_fluids();
    test_resource_location();
    test_gui_scale();
    test_title_layout();
    test_options_layout();
    test_fov_mapping();
    test_format_language_string();
    test_slider();
    test_splash_dates();
    test_widget_clicks();
    test_asset_index();
    test_audio_mixer();
    test_sound_registry();
    test_sound_system_and_music();
    test_now_playing_toast();
    test_zip_archive();
    test_nbt();
    test_options_file();
    test_game_options_round_trip();
    test_graphics_api_setting();
    test_gamemode_switcher_logic();
    test_chunk_cache();
    test_languages();
    test_ui_library_layout();
    test_select_world_layout();
    test_level_summary();
    test_nbt_writer();
    test_create_world_layout();
    test_new_world();
    test_base64();
    test_server_address();
    test_styled_text();
    test_server_list();
    test_server_ping();
    test_multiplayer_layout();
    test_key_mappings();
    test_key_binds_layout();
    test_world_storage();
    test_terrain();
    test_mesher();
    test_survival_hud();
    test_system_info();
    test_world_sounds();
    test_item_entities();
    test_world_save();
    test_ownership_answers();
    test_player_physics();
    test_block_registry();
    test_light();
    test_raycast();
    test_carried_light();
    test_camera_and_frustum();
    test_priority_pool_and_level_info();
    test_pause_layout();
    test_resource_layers();
    test_pack_repository_and_model();
    test_scroll_list();
    test_cycle_button();
    test_audio_devices();
    test_edition_badge();
    test_text_util();
    test_math();

    if (g_failures != 0) {
        std::cerr << g_failures << " check(s) failed\n";
        return 1;
    }
    std::cout << "All tests passed\n";
    return 0;
}
