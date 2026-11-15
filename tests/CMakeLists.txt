# Built only on request so ordinary builds stay fast:
#   cmake --build --preset check        (builds falcon_tests and runs it)
add_executable(falcon_tests EXCLUDE_FROM_ALL test_client.cpp)
target_link_libraries(falcon_tests PRIVATE falcon::core falcon_warnings)

add_test(NAME falcon_tests COMMAND falcon_tests)

# World generation (random sources, noise, density functions, chunk volumes): bit-exact checks against the game.
add_executable(falcon_worldgen_tests EXCLUDE_FROM_ALL test_worldgen.cpp)
target_link_libraries(falcon_worldgen_tests PRIVATE falcon::core falcon_warnings)
if(MSVC)
    target_compile_options(falcon_worldgen_tests PRIVATE /fp:strict)  # the expected values are computed with float ops too
else()
    target_compile_options(falcon_worldgen_tests PRIVATE -ffp-contract=off)
endif()
add_test(NAME falcon_worldgen_tests COMMAND falcon_worldgen_tests)

# Biomes, aquifer, ore veins, surface rules and whole chunks, against the game's own output (seed 12345).
add_executable(falcon_worldgen_chunk_tests EXCLUDE_FROM_ALL test_worldgen_chunks.cpp)
target_link_libraries(falcon_worldgen_chunk_tests PRIVATE falcon::core falcon_warnings)
target_compile_definitions(falcon_worldgen_chunk_tests PRIVATE FALCON_TEST_DATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/data")
if(MSVC)
    target_compile_options(falcon_worldgen_chunk_tests PRIVATE /fp:strict)
else()
    target_compile_options(falcon_worldgen_chunk_tests PRIVATE -ffp-contract=off)
endif()
add_test(NAME falcon_worldgen_chunk_tests COMMAND falcon_worldgen_chunk_tests)

# Items, inventory, menu click rules, crafting recipes, creative tabs, item models (no OpenGL needed).
add_executable(falcon_item_tests EXCLUDE_FROM_ALL test_items.cpp)
target_link_libraries(falcon_item_tests PRIVATE falcon::core falcon_warnings)
add_test(NAME falcon_item_tests COMMAND falcon_item_tests)

# Digging, loot, falling blocks, fire, TNT, redstone and the command tree, on the game's own data.
add_executable(falcon_world_tests EXCLUDE_FROM_ALL test_world_rules.cpp)
target_link_libraries(falcon_world_tests PRIVATE falcon::core falcon_warnings)
add_test(NAME falcon_world_tests COMMAND falcon_world_tests)

add_custom_target(check
    COMMAND "${CMAKE_CTEST_COMMAND}" --output-on-failure
    DEPENDS falcon_tests falcon_worldgen_tests falcon_worldgen_chunk_tests falcon_item_tests falcon_world_tests
    WORKING_DIRECTORY "${CMAKE_BINARY_DIR}"
    COMMENT "Running the unit tests"
    USES_TERMINAL
)
