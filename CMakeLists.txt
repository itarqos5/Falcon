cmake_minimum_required(VERSION 3.20)

project(Falcon
    VERSION 1.4.0
    DESCRIPTION "Falcon - a C++ reimplementation of the Minecraft Java client"
    LANGUAGES CXX C
)

if(WIN32)
    enable_language(RC)  # for the exe icon resource
endif()
if(APPLE)
    # The launcher's window host is Objective-C++ (WKWebView) and GLFW's Cocoa backend is Objective-C. Both are set up here, at the top of the project,
    # because that is where CMake loads the rules a language needs: enabling one from inside launcher/CMakeLists.txt leaves CMAKE_OBJCXX_COMPILE_OBJECT
    # unset. Objective-C is needed beside it, or CMake hands GLFW's .m files to the Objective-C++ rules and they fail on C's void pointers.
    enable_language(OBJC OBJCXX)
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Release unless asked otherwise: dist/ is what gets played, and a Debug exe is ten times bigger.
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()
# -O2 compiles noticeably faster than the default -O3 and runs the same for a GUI program;
# no debug info in Release keeps objects and archives small.
set(CMAKE_CXX_FLAGS_RELEASE "-O2 -DNDEBUG" CACHE STRING "" FORCE)
set(CMAKE_C_FLAGS_RELEASE   "-O2 -DNDEBUG" CACHE STRING "" FORCE)

option(FALCON_BUILD_TESTS "Build unit tests (only when asked: cmake --build --preset check)" ON)
option(FALCON_WARNINGS_AS_ERRORS "Treat compiler warnings as errors" OFF)
option(FALCON_CONSOLE "Keep a console window next to the game window (Windows)" OFF)
option(FALCON_CCACHE "Use ccache when it is installed (instant rebuilds of unchanged files)" ON)

# ccache keeps compiled objects outside the project, so even a deleted build/ rebuilds in seconds.
if(FALCON_CCACHE AND NOT CMAKE_CXX_COMPILER_LAUNCHER)
    find_program(FALCON_CCACHE_EXE ccache)
    if(FALCON_CCACHE_EXE)
        set(_falcon_ccache "${CMAKE_COMMAND}" -E env CCACHE_MAXSIZE=1G "${FALCON_CCACHE_EXE}")
        set(CMAKE_CXX_COMPILER_LAUNCHER ${_falcon_ccache})
        set(CMAKE_C_COMPILER_LAUNCHER   ${_falcon_ccache})
        message(STATUS "ccache: ${FALCON_CCACHE_EXE}")
    endif()
endif()

# ---------------------------------------------------------------------------
# Third-party dependencies (fetched at configure time, pinned)
# ---------------------------------------------------------------------------
include(FetchContent)
if(POLICY CMP0135)
    cmake_policy(SET CMP0135 NEW)  # extracted archives get the time of extraction
endif()
if(POLICY CMP0168)
    cmake_policy(SET CMP0168 NEW)  # fetch directly: no per-dependency sub-build, no second copy
endif()
set(FETCHCONTENT_QUIET ON)
set(FETCHCONTENT_UPDATES_DISCONNECTED ON)  # never touch the network once a dependency is present

# GLFW: window, input, OpenGL context
set(GLFW_BUILD_DOCS     OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS    OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(GLFW_INSTALL        OFF CACHE BOOL "" FORCE)
FetchContent_Declare(glfw
    GIT_REPOSITORY https://github.com/glfw/glfw.git
    GIT_TAG        3.4
    GIT_SHALLOW    TRUE
)

# nlohmann/json: font definitions, language files, sprite metadata
FetchContent_Declare(nlohmann_json
    URL      https://github.com/nlohmann/json/releases/download/v3.11.3/json.tar.xz
    URL_HASH SHA256=d6c65aca6b1ed68e7a182f4757257b107ae403032760ed6ef121c9d55e81757d
)

# stb: PNG decode / encode (single headers, pinned to a commit)
set(FALCON_STB_COMMIT 9600522de53973336fa033a289858a4354cbb04c)
FetchContent_Declare(stb_image
    URL      https://raw.githubusercontent.com/nothings/stb/${FALCON_STB_COMMIT}/stb_image.h
    URL_HASH SHA256=deb4f18c9ca21b751a1323019031c94c0bf5ca1d2929ef5913cc56f00aab6e91
    DOWNLOAD_NO_EXTRACT TRUE
)
FetchContent_Declare(stb_image_write
    URL      https://raw.githubusercontent.com/nothings/stb/${FALCON_STB_COMMIT}/stb_image_write.h
    URL_HASH SHA256=a8e7f77f0ccfdc9bac09a71268b131931ff81aafca82c393aeb6aaba10d9e7ca
    DOWNLOAD_NO_EXTRACT TRUE
)

# stb_vorbis: decodes the game's .ogg sounds and music
FetchContent_Declare(stb_vorbis
    URL      https://raw.githubusercontent.com/nothings/stb/${FALCON_STB_COMMIT}/stb_vorbis.c
    URL_HASH SHA256=b52cf518355f7b2d181f18b97b812f361ffeb26299d935c8cf29e842401d60a6
    DOWNLOAD_NO_EXTRACT TRUE
)

# miniaudio: audio output device (only the low-level device API is compiled in)
FetchContent_Declare(miniaudio
    URL      https://raw.githubusercontent.com/mackron/miniaudio/0.11.22/miniaudio.h
    URL_HASH SHA256=9019743287e443c55e5737a7297f38e5e358561701d6db2d905afb114390c410
    DOWNLOAD_NO_EXTRACT TRUE
)

FetchContent_MakeAvailable(glfw nlohmann_json stb_image stb_image_write stb_vorbis miniaudio)

# OpenSSL on a Mac is Homebrew's, in a prefix that is not searched by default.
if(APPLE AND NOT OPENSSL_ROOT_DIR)
    foreach(_falcon_brew "/opt/homebrew/opt/openssl@3" "/usr/local/opt/openssl@3")
        if(EXISTS "${_falcon_brew}/lib/libssl.dylib")
            set(OPENSSL_ROOT_DIR "${_falcon_brew}")
        endif()
    endforeach()
endif()

# The Vulkan renderer (src/vulkan/): only the headers are needed to build it. The loader is looked for when the game starts, so a PC without Vulkan still runs the game on OpenGL.
# (macOS has no Vulkan driver: Metal is the only one, and going through MoltenVK is not something this build does.)
if(APPLE)
    set(_falcon_vulkan_default OFF)
else()
    set(_falcon_vulkan_default ON)
endif()
option(FALCON_VULKAN "Build the Vulkan renderer (the Graphics API setting's second choice)" ${_falcon_vulkan_default})
option(FALCON_STRIP "Strip the symbols from the Release exes (OFF for a build that tools/resolve_samples.mjs can read the profiler's samples of)" ON)
# A fixed PE timestamp (and the header versions every Windows program normally carries), so the same sources and the same compiler give the same
# bytes: a hash published with a release then means one file, not "another build of the same code".
option(FALCON_REPRODUCIBLE "Give the Windows exes a fixed timestamp, so a build is reproducible byte for byte" ON)
if(FALCON_VULKAN)
    FetchContent_Declare(vulkan_headers
        GIT_REPOSITORY https://github.com/KhronosGroup/Vulkan-Headers.git
        GIT_TAG        vulkan-sdk-1.4.350.0
        GIT_SHALLOW    TRUE
    )
    FetchContent_MakeAvailable(vulkan_headers)

    # Only for changing a Vulkan shader: -DFALCON_COMPILE_SHADERS=ON fetches glslang (its libraries only, built from source, no SPIRV-Tools and none of its Python) and adds the
    # target falcon_vk_shaders, which compiles src/vulkan/shaders/ into src/vulkan/spirv_data.cpp. That file is committed, so building the game needs none of this.
    option(FALCON_COMPILE_SHADERS "Build glslang and add the target falcon_vk_shaders (compiles the Vulkan shaders)" OFF)
    if(FALCON_COMPILE_SHADERS)
        find_program(FALCON_NODE node REQUIRED)
        set(ENABLE_OPT OFF CACHE BOOL "" FORCE)
        set(ENABLE_HLSL OFF CACHE BOOL "" FORCE)
        set(ENABLE_GLSLANG_BINARIES OFF CACHE BOOL "" FORCE)
        set(ENABLE_SPVREMAPPER OFF CACHE BOOL "" FORCE)
        set(BUILD_EXTERNAL OFF CACHE BOOL "" FORCE)
        set(GLSLANG_TESTS OFF CACHE BOOL "" FORCE)
        set(GLSLANG_ENABLE_INSTALL OFF CACHE BOOL "" FORCE)
        FetchContent_Declare(glslang
            GIT_REPOSITORY https://github.com/KhronosGroup/glslang.git
            GIT_TAG        16.6.0
            GIT_SHALLOW    TRUE
        )
        FetchContent_MakeAvailable(glslang)
        add_executable(falcon_compile_spirv EXCLUDE_FROM_ALL tools/compile_spirv.cpp)
        target_link_libraries(falcon_compile_spirv PRIVATE glslang::glslang glslang::SPIRV glslang::glslang-default-resource-limits)
        add_custom_target(falcon_vk_shaders
            COMMAND "${FALCON_NODE}" "${CMAKE_CURRENT_SOURCE_DIR}/tools/compile_vk_shaders.mjs" "$<TARGET_FILE:falcon_compile_spirv>"
            DEPENDS falcon_compile_spirv
            WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
            COMMENT "Compiling the Vulkan shaders"
            VERBATIM)
    endif()
endif()

add_library(falcon_stb INTERFACE)
target_include_directories(falcon_stb SYSTEM INTERFACE
    "${stb_image_SOURCE_DIR}"
    "${stb_image_write_SOURCE_DIR}"
    "${stb_vorbis_SOURCE_DIR}"
    "${miniaudio_SOURCE_DIR}"
)

# ---------------------------------------------------------------------------
# Warnings (applied to our own targets only)
# ---------------------------------------------------------------------------
add_library(falcon_warnings INTERFACE)
if(MSVC)
    target_compile_options(falcon_warnings INTERFACE /W4 /permissive-)
    if(FALCON_WARNINGS_AS_ERRORS)
        target_compile_options(falcon_warnings INTERFACE /WX)
    endif()
else()
    target_compile_options(falcon_warnings INTERFACE -Wall -Wextra -Wpedantic -Wshadow)
    if(FALCON_WARNINGS_AS_ERRORS)
        target_compile_options(falcon_warnings INTERFACE -Werror)
    endif()
endif()

# Smaller exe and objects: every function/variable in its own section, unused ones dropped at link.
add_library(falcon_build_flags INTERFACE)
if(NOT MSVC)
    target_compile_options(falcon_build_flags INTERFACE -pipe -ffunction-sections -fdata-sections)
    if(APPLE)
        target_link_options(falcon_build_flags INTERFACE -Wl,-dead_strip)  # (ld64 has no --gc-sections)
    else()
        target_link_options(falcon_build_flags INTERFACE -Wl,--gc-sections)
    endif()
endif()

# The PE header Windows programs normally carry: an OS and subsystem version of 6.1 instead of GNU ld's 4.0/5.2 (a value from before Windows XP), and a
# timestamp taken from the content rather than the clock. Neither touches the code; both make the file look like (and be) an ordinary program that can be
# built twice to the same bytes. MSVC sets the same fields from the SDK defaults.
function(falcon_windows_link_options target)
    if(NOT WIN32 OR MSVC)
        return()
    endif()
    target_link_options(${target} PRIVATE -Wl,--major-os-version,6 -Wl,--minor-os-version,1)
    target_link_options(${target} PRIVATE -Wl,--major-subsystem-version,6 -Wl,--minor-subsystem-version,1)
    if(FALCON_REPRODUCIBLE)
        target_link_options(${target} PRIVATE -Wl,--no-insert-timestamp)
    endif()
endfunction()

# ---------------------------------------------------------------------------
# Generated version header
# ---------------------------------------------------------------------------
set(FALCON_GENERATED_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated")
configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/include/falcon/version.hpp.in"
    "${FALCON_GENERATED_DIR}/falcon/version.hpp"
    @ONLY
)

# ---------------------------------------------------------------------------
# Base library: reading assets, text and fonts, OpenGL and the window. No UI, no game.
# ---------------------------------------------------------------------------
add_library(falcon_base
    src/util/math.cpp
    src/util/text_util.cpp
    src/util/options_file.cpp
    src/util/log.cpp
    src/util/base64.cpp
    src/util/server_address.cpp
    src/assets/resource_location.cpp
    src/assets/asset_locator.cpp
    src/assets/asset_index.cpp
    src/assets/zip_archive.cpp
    src/assets/resource_source.cpp
    src/assets/resource_manager.cpp
    src/assets/nbt.cpp
    src/assets/image.cpp
    src/mods/mod_info.cpp
    src/mods/optimization_mods.cpp
    src/jvm/class_file.cpp
    src/jvm/vm.cpp
    src/jvm/interp.cpp
    src/jvm/natives.cpp
    src/jvm/shims.cpp
    src/jvm/shims_game.cpp
    src/jvm/host_natives.cpp
    src/jvm/reflect_natives.cpp
    src/jvm/modapi.cpp
    src/jvm/mod_content.cpp
    src/jvm/mod_runtime.cpp
    src/text/text_layout.cpp
    src/text/styled_text.cpp
    src/text/language.cpp
    src/text/font.cpp
    src/gfx/gl.cpp
    src/gfx/shader.cpp
    src/gfx/texture.cpp
    src/gfx/texture_array.cpp
    src/gfx/mipmaps.cpp
    src/gfx/graphics_api.cpp
    src/gfx/texture_manager.cpp
    src/gfx/gui_renderer.cpp
    src/gfx/render_target.cpp
    src/gfx/panorama_renderer.cpp
    src/platform/window.cpp
    src/platform/dirs.cpp
    src/platform/key_names.cpp
    src/platform/dialog.cpp
    src/platform/clipboard.cpp
    src/platform/memory.cpp
    src/platform/system_info.cpp
    src/platform/ownership.cpp
    src/platform/http.cpp
    src/platform/mapped_file.cpp
    src/platform/file_lock.cpp
    src/platform/sampling_profiler.cpp
)
add_library(falcon::base ALIAS falcon_base)
if(FALCON_VULKAN)
    # (VK_NO_PROTOTYPES: every Vulkan function is loaded by hand, so nothing is linked against a Vulkan library.)
    target_sources(falcon_base PRIVATE
        src/vulkan/vk_api.cpp
        src/vulkan/vk_core.cpp
        src/vulkan/vk_swapchain.cpp
        src/vulkan/vk_backend.cpp
        src/vulkan/spirv_reflect.cpp
        src/vulkan/shader_registry.cpp
        src/vulkan/spirv_data.cpp
        src/vulkan/gl_buffers.cpp
        src/vulkan/gl_textures.cpp
        src/vulkan/gl_programs.cpp
        src/vulkan/gl_draw.cpp
        src/vulkan/gl_misc.cpp
    )
    target_compile_definitions(falcon_base PUBLIC FALCON_HAS_VULKAN=1)
    target_compile_definitions(falcon_base PRIVATE VK_NO_PROTOTYPES)
    target_link_libraries(falcon_base PRIVATE Vulkan::Headers)
endif()
target_include_directories(falcon_base PUBLIC
    "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>"
    "$<BUILD_INTERFACE:${FALCON_GENERATED_DIR}>"
)
target_compile_definitions(falcon_base PRIVATE FALCON_MODAPI_DEV_JAR="${CMAKE_SOURCE_DIR}/modapi/falcon-modapi.jar")  # (the platform jar of the source tree: see src/jvm/modapi.cpp)
target_link_libraries(falcon_base
    PUBLIC  glfw nlohmann_json::nlohmann_json falcon_build_flags
    PRIVATE falcon_stb falcon_warnings
)
if(WIN32)
    target_link_libraries(falcon_base PRIVATE user32 psapi dwmapi winmm winhttp)  # message boxes, console attach; memory use; dark title bar; the ownership check
elseif(APPLE)
    # A Mac: HTTPS (the ownership check, the session server, skins) and the cryptography of a server in online mode are OpenSSL's (Homebrew's); GLFW brings in the
    # Cocoa, IOKit and CoreVideo frameworks its window needs.
    find_package(Threads REQUIRED)
    find_package(OpenSSL 1.1.1 REQUIRED)
    target_sources(falcon_base PRIVATE src/platform/tls_http.cpp)  # HTTPS: the part the game and the launcher share
    target_link_libraries(falcon_base PUBLIC OpenSSL::SSL OpenSSL::Crypto Threads::Threads ${CMAKE_DL_LIBS})
    target_link_libraries(falcon_base PRIVATE "-framework CoreFoundation" "-framework Security")
else()
    # Linux: HTTPS (the ownership check, the session server, skins) and the cryptography of a server in online mode are OpenSSL's; threads and dlopen for
    # the audio backends miniaudio loads at run time.
    find_package(Threads REQUIRED)
    option(FALCON_STATIC_OPENSSL "Link OpenSSL into the program (the released build does: no libssl on the player's PC is needed)" OFF)
    if(FALCON_STATIC_OPENSSL)
        set(OPENSSL_USE_STATIC_LIBS TRUE)
    endif()
    find_package(OpenSSL 1.1.1 REQUIRED)
    target_sources(falcon_base PRIVATE src/platform/tls_http.cpp)  # HTTPS: the part the game and the launcher share
    target_link_libraries(falcon_base PUBLIC OpenSSL::SSL OpenSSL::Crypto Threads::Threads ${CMAKE_DL_LIBS})
endif()

# ---------------------------------------------------------------------------
# UI library: Minecraft-style widgets, screens and layout helpers. Depends only on falcon_base,
# never on game code (the compiler enforces it); see docs/UI.md.
# ---------------------------------------------------------------------------
add_library(falcon_ui
    src/ui/sprite_manager.cpp
    src/ui/gui_graphics.cpp
    src/ui/widgets.cpp
    src/ui/text_edit.cpp
    src/ui/screen.cpp
    src/ui/scroll_list.cpp
    src/ui/list_screen.cpp
    src/ui/confirm_screen.cpp
)
add_library(falcon::ui ALIAS falcon_ui)
target_link_libraries(falcon_ui
    PUBLIC  falcon_base
    PRIVATE falcon_warnings
)

# ---------------------------------------------------------------------------
# Game: sound, resource packs, worlds, every concrete screen, and the client loop.
# ---------------------------------------------------------------------------
add_library(falcon_core
    src/client.cpp
    src/client_options.cpp
    src/client_audio.cpp
    src/resource_reloader.cpp
    src/packs/pack_repository.cpp
    src/packs/pack_selection_model.cpp
    src/world/level_summary.cpp
    src/world/new_world.cpp
    src/world/block_registry.cpp
    src/world/item_registry.cpp
    src/world/item_stack.cpp
    src/world/inventory.cpp
    src/world/recipes.cpp
    src/world/menu.cpp
    src/world/chunk.cpp
    src/world/light.cpp
    src/world/decoration.cpp
    src/world/terrain.cpp
    src/world/mesher.cpp
    src/world/level_info.cpp
    src/world/player.cpp
    src/world/player_movement.cpp
    src/world/boat.cpp
    src/world/item_entity.cpp
    src/world/day_cycle.cpp
    src/world/mob.cpp
    src/world/mob_path.cpp
    src/world/mob_spawn.cpp
    src/world/random_ticks.cpp
    src/world/nether_decoration.cpp
    src/world/world_save.cpp
    src/world/block_containers.cpp
    src/world/collision_shapes.cpp
    src/world/block_outlines.cpp
    src/world/block_support.cpp
    src/world/block_tags.cpp
    src/world/block_behaviours.cpp
    src/world/block_physics.cpp
    src/world/redstone.cpp
    src/world/loot.cpp
    src/world/chunk_cache.cpp
    src/world/raycast.cpp
    src/world/water_fog.cpp
    src/world/fluids.cpp
    src/render/block_models.cpp
    src/render/block_icons.cpp
    src/render/hand_renderer.cpp
    src/render/item_icons.cpp
    src/render/entity_model.cpp
    src/render/player_preview.cpp
    src/render/player_body.cpp
    src/render/mob_renderer.cpp
    src/render/world_renderer.cpp
    src/render/lod_renderer.cpp
    src/render/sprite_renderer.cpp
    src/world/server_list.cpp
    src/worldgen/random.cpp
    src/worldgen/json_tree.cpp
    src/render/world_renderer_draw.cpp
    src/render/world_renderer_edit.cpp
    src/worldgen/noise.cpp
    src/worldgen/random_state.cpp
    src/worldgen/density.cpp
    src/worldgen/density_build.cpp
    src/worldgen/noise_router.cpp
    src/worldgen/biome.cpp
    src/worldgen/aquifer.cpp
    src/worldgen/legacy_noise.cpp
    src/worldgen/material.cpp
    src/worldgen/overworld_generator.cpp
    src/worldgen/underground.cpp
    src/worldgen/nether_carver.cpp
    src/worldgen/lod_surface.cpp
    src/net/server_pinger.cpp
    src/net/connection.cpp
    src/net/protocol_data.cpp
    src/net/multiplayer_client.cpp
    src/net/crypto.cpp
    src/net/session_auth.cpp
    src/net/last_seen.cpp
    src/net/skin_fetcher.cpp
    src/net/server_commands.cpp
    src/audio/vorbis_impl.c
    src/audio/miniaudio_impl.cpp
    src/audio/ogg.cpp
    src/audio/audio_engine.cpp
    src/audio/sound_registry.cpp
    src/audio/sound_system.cpp
    src/audio/music_manager.cpp
    src/game/panorama.cpp
    src/game/edition_badge.cpp
    src/game/splash_manager.cpp
    src/game/title_screen.cpp
    src/game/options_screen.cpp
    src/game/now_playing_toast.cpp
    src/game/menu_background.cpp
    src/game/game_options.cpp
    src/game/key_mappings.cpp
    src/game/game_options_screen.cpp
    src/game/options_screens.cpp
    src/game/options_video_screens.cpp
    src/game/advancements.cpp
    src/game/progress_screens.cpp
    src/game/language_screen.cpp
    src/game/select_world_screen.cpp
    src/game/key_binds_screen.cpp
    src/game/game_session.cpp
    src/game/in_game_screen.cpp
    src/game/mod_screen.cpp
    src/game/death_screen.cpp
    src/game/lighting_audit.cpp
    src/game/vanilla_compare.cpp
    src/game/chat.cpp
    src/game/chat_screen.cpp
    src/game/gamemode_switcher_screen.cpp
    src/game/block_placement.cpp
    src/game/mining.cpp
    src/game/command_tree.cpp
    src/game/game_commands.cpp
    src/game/creative_tabs.cpp
    src/game/creative_inventory.cpp
    src/game/item_names.cpp
    src/game/game_session_player.cpp
    src/game/game_session_dimensions.cpp
    src/game/game_session_portals.cpp
    src/game/game_session_dragon.cpp
    src/game/game_session_buckets.cpp
    src/game/game_session_remote.cpp
    src/game/game_session_blocks.cpp
    src/game/game_session_containers.cpp
    src/game/game_session_use.cpp
    src/game/game_session_render.cpp
    src/game/fps_test.cpp
    src/game/game_session_debug.cpp
    src/game/game_session_mods.cpp
    src/game/mod_content_apply.cpp
    src/game/mods_screen.cpp
    src/game/item_render.cpp
    src/game/container_screen.cpp
    src/game/inventory_screen.cpp
    src/game/recipe_book.cpp
    src/game/create_world_screen.cpp
    src/game/join_multiplayer_screen.cpp
    src/game/server_screens.cpp
    src/game/safety_screen.cpp
    src/game/disconnected_screen.cpp
    src/game/remote_menu.cpp
    src/game/remote_container_screen.cpp
    src/game/sound_options_screen.cpp
    src/game/pack_selection_screen.cpp
)
add_library(falcon::core ALIAS falcon_core)
if(WIN32)
    target_link_libraries(falcon_core PUBLIC ws2_32 dnsapi bcrypt)  # server status pings: sockets and the SRV lookup; the login of a server in online mode: RSA, AES, SHA
elseif(APPLE)
    target_link_libraries(falcon_core PUBLIC resolv)  # the SRV lookup (res_query, in libresolv)
    # miniaudio plays through CoreAudio there, which is these frameworks; nothing else has to be installed.
    target_link_libraries(falcon_core PRIVATE "-framework CoreAudio" "-framework AudioToolbox" "-framework CoreFoundation" "-framework CoreMIDI")
else()
    target_link_libraries(falcon_core PUBLIC resolv)  # the SRV lookup (res_query: in libc since glibc 2.34, in libresolv before)
endif()
target_link_libraries(falcon_core
    PUBLIC  falcon_ui
    PRIVATE falcon_stb falcon_warnings
)
# miniaudio: only the playback device API (we decode with stb_vorbis and mix ourselves). These
# change struct layouts, so they live here and every file that includes miniaudio.h sees the same
# list. On Windows only WASAPI (with WinMM as a fallback) is compiled, which also makes the
# single-file implementation compile much faster.
target_compile_definitions(falcon_core PRIVATE
    MA_NO_DECODING MA_NO_ENCODING MA_NO_GENERATION MA_NO_ENGINE MA_NO_RESOURCE_MANAGER
    MA_NO_NODE_GRAPH)
if(WIN32)
    target_compile_definitions(falcon_core PRIVATE
        MA_ENABLE_ONLY_SPECIFIC_BACKENDS MA_ENABLE_WASAPI MA_ENABLE_WINMM)
elseif(APPLE)
    target_compile_definitions(falcon_core PRIVATE
        MA_ENABLE_ONLY_SPECIFIC_BACKENDS MA_ENABLE_COREAUDIO)  # (one backend to compile, as on Windows)
endif()
# Third-party single-file implementations: not ours to fix warnings in.
if(MSVC)
    set_source_files_properties(src/audio/vorbis_impl.c src/audio/miniaudio_impl.cpp
                                PROPERTIES COMPILE_OPTIONS "/w")
else()
    set_source_files_properties(src/audio/vorbis_impl.c src/audio/miniaudio_impl.cpp
                                PROPERTIES COMPILE_OPTIONS "-w")
endif()
# Terrain generation is bit-exact with the game's float arithmetic: never fuse a multiply and an add (GCC contracts
# by default), never use -ffast-math.
if(MSVC)
    set_source_files_properties(src/worldgen/noise.cpp src/worldgen/density.cpp src/worldgen/density_build.cpp src/worldgen/noise_router.cpp
                                src/worldgen/biome.cpp src/worldgen/aquifer.cpp src/worldgen/legacy_noise.cpp src/worldgen/material.cpp
                                src/worldgen/overworld_generator.cpp
                                PROPERTIES COMPILE_OPTIONS "/fp:strict")
else()
    set_source_files_properties(src/worldgen/noise.cpp src/worldgen/density.cpp src/worldgen/density_build.cpp src/worldgen/noise_router.cpp
                                src/worldgen/biome.cpp src/worldgen/aquifer.cpp src/worldgen/legacy_noise.cpp src/worldgen/material.cpp
                                src/worldgen/overworld_generator.cpp
                                PROPERTIES COMPILE_OPTIONS "-ffp-contract=off")
endif()

# ---------------------------------------------------------------------------
# Executable
# ---------------------------------------------------------------------------
add_executable(falcon_client src/main.cpp)
target_link_libraries(falcon_client PRIVATE falcon::core falcon_warnings)
set_target_properties(falcon_client PROPERTIES OUTPUT_NAME "Falcon")

if(MINGW)
    # Ship a self-contained exe: no dependency on the MinGW runtime DLLs being on PATH.
    target_link_options(falcon_client PRIVATE -static)
elseif(UNIX AND CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    # Linux: the C++ runtime goes inside the program, so it does not need the (newer) libstdc++ of the machine that built it. glibc stays dynamic.
    target_link_options(falcon_client PRIVATE -static-libstdc++ -static-libgcc)
endif()
if(NOT MSVC)
    # Release: strip symbols from the shipped exe (a large part of its size).
    if(FALCON_STRIP)
        target_link_options(falcon_client PRIVATE $<$<CONFIG:Release>:-s>)
    endif()
endif()
falcon_windows_link_options(falcon_client)

# Every build ends by replacing dist/ with the fresh game, wherever the intermediates live.
set(FALCON_DIST_DIR "${CMAKE_CURRENT_SOURCE_DIR}/dist" CACHE PATH "Where the finished game is placed")
add_custom_command(TARGET falcon_client POST_BUILD
    COMMAND "${CMAKE_COMMAND}"
            "-DDIST_DIR=${FALCON_DIST_DIR}"
            "-DEXE=$<TARGET_FILE:falcon_client>"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/UpdateDist.cmake"
    COMMENT "Replacing dist/ with the new build"
    VERBATIM
)

# No terminal window behind the game. (main() keeps working: the linker's entry point calls it.)
if(WIN32 AND NOT FALCON_CONSOLE)
    if(MSVC)
        target_link_options(falcon_client PRIVATE /SUBSYSTEM:WINDOWS /ENTRY:mainCRTStartup)
    else()
        target_link_options(falcon_client PRIVATE -mwindows)
    endif()
endif()

# The exe's own icon (Explorer, the pinned taskbar shortcut) and the window's title-bar / taskbar icon are
# the Falcon logo: resources/falcon_icon.png is scaled to the standard icon sizes at build time and packed
# into an .ico that is linked into the exe as a resource, together with the file's version information
# (the name Task Manager and the file's Properties dialog show).
if(WIN32)
    set(FALCON_ICON_PNG "${CMAKE_CURRENT_SOURCE_DIR}/resources/falcon_icon.png")
    add_executable(falcon_make_ico tools/make_ico.cpp)
    target_link_libraries(falcon_make_ico PRIVATE falcon_stb)
    set(FALCON_ICO "${CMAKE_CURRENT_BINARY_DIR}/falcon_icon.ico")
    add_custom_command(
        OUTPUT "${FALCON_ICO}"
        COMMAND falcon_make_ico "${FALCON_ICO}" "${FALCON_ICON_PNG}"
        DEPENDS falcon_make_ico "${FALCON_ICON_PNG}"
        COMMENT "Building the Falcon app icon"
        VERBATIM
    )
    # GLFW looks for a resource named GLFW_ICON for its window class, and Explorer shows the first icon in
    # the exe, which is this one. (The window also asks for its icon sizes by name at run time.)
    set(FALCON_RC "${CMAKE_CURRENT_BINARY_DIR}/falcon.rc")
    file(WRITE "${FALCON_RC}"
"GLFW_ICON ICON \"${FALCON_ICO}\"

1 VERSIONINFO
FILEVERSION ${PROJECT_VERSION_MAJOR},${PROJECT_VERSION_MINOR},${PROJECT_VERSION_PATCH},0
PRODUCTVERSION ${PROJECT_VERSION_MAJOR},${PROJECT_VERSION_MINOR},${PROJECT_VERSION_PATCH},0
{
    BLOCK \"StringFileInfo\"
    {
        BLOCK \"040904B0\"
        {
            VALUE \"FileDescription\", \"Falcon\"
            VALUE \"ProductName\", \"Falcon\"
            VALUE \"InternalName\", \"Falcon\"
            VALUE \"OriginalFilename\", \"Falcon.exe\"
            VALUE \"FileVersion\", \"${PROJECT_VERSION}\"
            VALUE \"ProductVersion\", \"${PROJECT_VERSION}\"
            VALUE \"CompanyName\", \"Falcon project\"
            VALUE \"LegalCopyright\", \"Copyright (c) 2026 Falcon contributors. Apache License 2.0.\"
            VALUE \"Comments\", \"https://github.com/itarqos5/Falcon\"
        }
    }
    BLOCK \"VarFileInfo\"
    {
        VALUE \"Translation\", 0x409, 1200
    }
}
")
    set_source_files_properties("${FALCON_RC}" PROPERTIES OBJECT_DEPENDS "${FALCON_ICO}")
    # Compiled in its own target so windres gets no -I flags: it splits quoted include paths
    # at spaces, which breaks when the checkout lives in a folder with a space in its name.
    add_library(falcon_rc OBJECT "${FALCON_RC}")
    # (the launcher's resource file uses the same icon file)
    add_custom_target(falcon_icon DEPENDS "${FALCON_ICO}")
    target_sources(falcon_client PRIVATE $<TARGET_OBJECTS:falcon_rc>)
endif()

# The client that goes into the launcher: the same game, but it only starts for an account that owns Minecraft: Java Edition. The launcher gives it the account's
# Minecraft access token and the client asks Mojang's servers about it itself (src/platform/ownership.cpp), so the check cannot be passed with a made-up answer. The
# Falcon.exe in dist/ has no such check, for development, tests and screenshots; anyone who builds Falcon from source can build either (it is a licence check, not
# copy protection). Built only as part of the launcher: cmake --build --preset launcher.
add_executable(falcon_client_licensed EXCLUDE_FROM_ALL src/main.cpp)
target_compile_definitions(falcon_client_licensed PRIVATE FALCON_REQUIRE_OWNERSHIP=1)
target_link_libraries(falcon_client_licensed PRIVATE falcon::core falcon_warnings)
set_target_properties(falcon_client_licensed PROPERTIES OUTPUT_NAME "Falcon" RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/licensed")
if(MINGW)
    target_link_options(falcon_client_licensed PRIVATE -static)
elseif(UNIX AND CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    target_link_options(falcon_client_licensed PRIVATE -static-libstdc++ -static-libgcc)
endif()
if(NOT MSVC)
    if(FALCON_STRIP)
        target_link_options(falcon_client_licensed PRIVATE $<$<CONFIG:Release>:-s>)
    endif()
endif()
falcon_windows_link_options(falcon_client_licensed)
if(WIN32 AND NOT FALCON_CONSOLE)
    if(MSVC)
        target_link_options(falcon_client_licensed PRIVATE /SUBSYSTEM:WINDOWS /ENTRY:mainCRTStartup)
    else()
        target_link_options(falcon_client_licensed PRIVATE -mwindows)
    endif()
endif()
if(TARGET falcon_rc)
    target_sources(falcon_client_licensed PRIVATE $<TARGET_OBJECTS:falcon_rc>)
endif()

# ---------------------------------------------------------------------------
# Tests
# ---------------------------------------------------------------------------
if(FALCON_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

# ---------------------------------------------------------------------------
# The launcher: a native window with a React UI in the system's web view (WebView2 on Windows, WebKitGTK on Linux). Not part of the ordinary build (it needs Node, and builds
# the game first): cmake --build --preset launcher. See launcher/README.md.
# ---------------------------------------------------------------------------
if(WIN32 OR APPLE)
    set(_falcon_launcher_default ON)
else()
    set(_falcon_launcher_default OFF)  # (on Linux it needs GTK and WebKitGTK to be installed, which the game does not: ask for it with -DFALCON_BUILD_LAUNCHER=ON)
endif()
option(FALCON_BUILD_LAUNCHER "Make the launcher target available (cmake --build --preset launcher)" ${_falcon_launcher_default})
if(FALCON_BUILD_LAUNCHER)
    add_subdirectory(launcher)
endif()
