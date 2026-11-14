# Built only on request so ordinary builds stay fast:
#   cmake --build --preset check        (builds apex_tests and runs it)
add_executable(apex_tests EXCLUDE_FROM_ALL test_client.cpp)
target_link_libraries(apex_tests PRIVATE apex::core apex_warnings)

add_test(NAME apex_tests COMMAND apex_tests)

# World generation (random sources, noise, density functions, chunk volumes): bit-exact checks against the game.
add_executable(apex_worldgen_tests EXCLUDE_FROM_ALL test_worldgen.cpp)
target_link_libraries(apex_worldgen_tests PRIVATE apex::core apex_warnings)
if(MSVC)
    target_compile_options(apex_worldgen_tests PRIVATE /fp:strict)  # the expected values are computed with float ops too
else()
    target_compile_options(apex_worldgen_tests PRIVATE -ffp-contract=off)
endif()
add_test(NAME apex_worldgen_tests COMMAND apex_worldgen_tests)

# Biomes, aquifer, ore veins, surface rules and whole chunks, against the game's own output (seed 12345).
add_executable(apex_worldgen_chunk_tests EXCLUDE_FROM_ALL test_worldgen_chunks.cpp)
target_link_libraries(apex_worldgen_chunk_tests PRIVATE apex::core apex_warnings)
target_compile_definitions(apex_worldgen_chunk_tests PRIVATE APEX_TEST_DATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/data")
if(MSVC)
    target_compile_options(apex_worldgen_chunk_tests PRIVATE /fp:strict)
else()
    target_compile_options(apex_worldgen_chunk_tests PRIVATE -ffp-contract=off)
endif()
add_test(NAME apex_worldgen_chunk_tests COMMAND apex_worldgen_chunk_tests)

# Items, inventory, menu click rules, crafting recipes, creative tabs, item models (no OpenGL needed).
add_executable(apex_item_tests EXCLUDE_FROM_ALL test_items.cpp)
target_link_libraries(apex_item_tests PRIVATE apex::core apex_warnings)
add_test(NAME apex_item_tests COMMAND apex_item_tests)

add_custom_target(check
    COMMAND "${CMAKE_CTEST_COMMAND}" --output-on-failure
    DEPENDS apex_tests apex_worldgen_tests apex_worldgen_chunk_tests apex_item_tests
    WORKING_DIRECTORY "${CMAKE_BINARY_DIR}"
    COMMENT "Running the unit tests"
    USES_TERMINAL
)
