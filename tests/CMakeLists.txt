# Built only on request so ordinary builds stay fast:
#   cmake --build --preset check        (builds falcon_tests and runs it)
add_executable(falcon_tests EXCLUDE_FROM_ALL test_client.cpp test_client_core.cpp test_client_audio_zip_save.cpp test_client_options_world.cpp test_client_server_keys.cpp
    test_client_mesh_player.cpp test_client_player_world.cpp)
target_link_libraries(falcon_tests PRIVATE falcon::core falcon_warnings)

add_test(NAME falcon_tests COMMAND falcon_tests)

# World generation (random sources, noise, density functions, chunk volumes): bit-exact checks against the game.
add_executable(falcon_worldgen_tests EXCLUDE_FROM_ALL test_worldgen.cpp test_worldgen_basics.cpp test_worldgen_noise_tables.cpp test_worldgen_router.cpp)
target_link_libraries(falcon_worldgen_tests PRIVATE falcon::core falcon_warnings)
if(MSVC)
    target_compile_options(falcon_worldgen_tests PRIVATE /fp:strict)  # the expected values are computed with float ops too
else()
    target_compile_options(falcon_worldgen_tests PRIVATE -ffp-contract=off)
endif()
add_test(NAME falcon_worldgen_tests COMMAND falcon_worldgen_tests)

# Biomes, aquifer, ore veins, surface rules and whole chunks, against the game's own output (seed 12345).
add_executable(falcon_worldgen_chunk_tests EXCLUDE_FROM_ALL test_worldgen_chunks.cpp test_worldgen_chunks_biomes.cpp test_worldgen_chunks_truth.cpp test_worldgen_chunks_primitives.cpp)
target_link_libraries(falcon_worldgen_chunk_tests PRIVATE falcon::core falcon_warnings)
target_compile_definitions(falcon_worldgen_chunk_tests PRIVATE FALCON_TEST_DATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/data")
if(MSVC)
    target_compile_options(falcon_worldgen_chunk_tests PRIVATE /fp:strict)
else()
    target_compile_options(falcon_worldgen_chunk_tests PRIVATE -ffp-contract=off)
endif()
add_test(NAME falcon_worldgen_chunk_tests COMMAND falcon_worldgen_chunk_tests)

# Items, inventory, menu click rules, crafting recipes, creative tabs, item models (no OpenGL needed).
add_executable(falcon_item_tests EXCLUDE_FROM_ALL test_items.cpp test_items_menus.cpp test_items_recipes_real.cpp)
target_link_libraries(falcon_item_tests PRIVATE falcon::core falcon_warnings)
add_test(NAME falcon_item_tests COMMAND falcon_item_tests)

# Digging, loot, falling blocks, fire, TNT, redstone and the command tree, on the game's own data.
add_executable(falcon_world_tests EXCLUDE_FROM_ALL test_world_rules.cpp)
target_link_libraries(falcon_world_tests PRIVATE falcon::core falcon_warnings)
add_test(NAME falcon_world_tests COMMAND falcon_world_tests)

# The levels of detail: how the columns of a tile are meshed (no graphics card needed).
add_executable(falcon_lod_tests EXCLUDE_FROM_ALL test_lod.cpp)
target_link_libraries(falcon_lod_tests PRIVATE falcon::core falcon_warnings)
add_test(NAME falcon_lod_tests COMMAND falcon_lod_tests)

# The game's rules that need no data and no graphics card: the clock, and what the mobs and the portals do.
add_executable(falcon_logic_tests EXCLUDE_FROM_ALL test_logic.cpp test_logic_day.cpp)
target_sources(falcon_logic_tests PRIVATE test_logic_mob.cpp)
target_sources(falcon_logic_tests PRIVATE test_logic_random.cpp)
target_sources(falcon_logic_tests PRIVATE test_logic_end.cpp)
target_link_libraries(falcon_logic_tests PRIVATE falcon::core falcon_warnings)
add_test(NAME falcon_logic_tests COMMAND falcon_logic_tests)

# The multiplayer protocol's data: the byte codec and item stacks as the network writes them.
add_executable(falcon_net_tests EXCLUDE_FROM_ALL test_net_protocol.cpp)
target_link_libraries(falcon_net_tests PRIVATE falcon::core falcon_warnings)
add_test(NAME falcon_net_tests COMMAND falcon_net_tests)

add_custom_target(check
    COMMAND "${CMAKE_CTEST_COMMAND}" --output-on-failure
    DEPENDS falcon_tests falcon_worldgen_tests falcon_worldgen_chunk_tests falcon_item_tests falcon_world_tests falcon_net_tests falcon_lod_tests falcon_logic_tests
    WORKING_DIRECTORY "${CMAKE_BINARY_DIR}"
    COMMENT "Running the unit tests"
    USES_TERMINAL
)

# A world folder without any screen (level.dat and session.lock, as Create New World writes them): for scripted runs that open a world with --play (the CI builds, the benchmarks).
add_executable(falcon_make_world EXCLUDE_FROM_ALL ../tools/make_world.cpp)
target_link_libraries(falcon_make_world PRIVATE falcon::core falcon_warnings)

# How far apart two pictures of a frame are (the Vulkan renderer against the OpenGL one in the CI build).
add_executable(falcon_image_diff EXCLUDE_FROM_ALL ../tools/image_diff.cpp)
target_link_libraries(falcon_image_diff PRIVATE falcon::core falcon_warnings)

# The Vulkan shader twins, read back from their SPIR-V (no graphics card needed).
if(FALCON_VULKAN)
    add_executable(falcon_vulkan_tests EXCLUDE_FROM_ALL test_vulkan_shaders.cpp)
    target_link_libraries(falcon_vulkan_tests PRIVATE falcon::core falcon_warnings)
    add_test(NAME falcon_vulkan_tests COMMAND falcon_vulkan_tests)
    add_dependencies(check falcon_vulkan_tests)
endif()

# How fast chunks are made, and a hash of them (tools/bench_chunks.cpp): for making generation faster without changing a block.
add_executable(falcon_bench_chunks EXCLUDE_FROM_ALL ../tools/bench_chunks.cpp)
target_link_libraries(falcon_bench_chunks PRIVATE falcon::core falcon_warnings)

# What the Nether's and the End's generators make (a look at the terrain without starting the game): tools/dim_probe.cpp
add_executable(falcon_dim_probe EXCLUDE_FROM_ALL ../tools/dim_probe.cpp)
target_link_libraries(falcon_dim_probe PRIVATE falcon::core falcon_warnings)
