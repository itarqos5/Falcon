# Falcon Launcher: a native Windows app (C++20) whose UI is a React app shown by the system's WebView2 runtime (no browser is bundled).
#
#   cmake --build --preset launcher        builds dist-launcher/FalconLauncher.exe (and the client and the UI it embeds)
#
# Not part of the ordinary build: it needs Node (for the React UI) and builds the game first.
if(NOT WIN32)
    return()
endif()

# ---------------------------------------------------------------------------
# WebView2 SDK: the headers and the 190 KB loader DLL (which finds the runtime Windows already has).
# ---------------------------------------------------------------------------
set(FALCON_WEBVIEW2_VERSION 1.0.4314.35)
FetchContent_Declare(webview2
    URL           https://api.nuget.org/v3-flatcontainer/microsoft.web.webview2/${FALCON_WEBVIEW2_VERSION}/microsoft.web.webview2.${FALCON_WEBVIEW2_VERSION}.nupkg
    URL_HASH      SHA256=90778DEB1B77E36292828B4B5833C9255C27533F40E0DDD55891AE718944EDC4
    DOWNLOAD_NAME microsoft.web.webview2.${FALCON_WEBVIEW2_VERSION}.zip
)
FetchContent_MakeAvailable(webview2)
set(WV2_INCLUDE "${webview2_SOURCE_DIR}/build/native/include")
set(WV2_LOADER  "${webview2_SOURCE_DIR}/build/native/x64/WebView2Loader.dll")

# The Microsoft account's Azure application (client) id. Without one the launcher builds and runs but cannot sign anyone in: see
# launcher/README.md ("Registering the Azure app"). It can also be given at run time (FALCON_MS_CLIENT_ID, or launcher.json).
set(FALCON_LAUNCHER_MS_CLIENT_ID "" CACHE STRING "Azure application (client) id used for the Microsoft sign-in")

# ---------------------------------------------------------------------------
# The logic (no window): HTTP, sign-in, game files, setup, the controller the UI talks to. Tested on its own.
# ---------------------------------------------------------------------------
add_library(falcon_launcher_core STATIC EXCLUDE_FROM_ALL
    src/util.cpp
    src/http.cpp
    src/sha1.cpp
    src/secure_store.cpp
    src/auth.cpp
    src/game_files.cpp
    src/setup.cpp
    src/app.cpp
    src/stb_impl.cpp
    src/avatar.cpp
    "${CMAKE_SOURCE_DIR}/src/assets/zip_archive.cpp"
    "${CMAKE_SOURCE_DIR}/src/platform/mapped_file.cpp"
)
target_include_directories(falcon_launcher_core PUBLIC src "${CMAKE_SOURCE_DIR}/include")
target_compile_definitions(falcon_launcher_core PUBLIC
    UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX _WIN32_WINNT=0x0A00
    FALCON_LAUNCHER_MS_CLIENT_ID="${FALCON_LAUNCHER_MS_CLIENT_ID}"
    FALCON_LAUNCHER_VERSION="${PROJECT_VERSION}")
target_link_libraries(falcon_launcher_core PUBLIC nlohmann_json::nlohmann_json falcon_stb falcon_warnings
    winhttp bcrypt crypt32 ole32 shell32 shlwapi uuid advapi32)

# ---------------------------------------------------------------------------
# The React UI: built by Vite into ONE html file (script and styles inline), which the exe carries and serves from memory.
# ---------------------------------------------------------------------------
find_program(FALCON_NPM npm)
set(UI_DIR  "${CMAKE_CURRENT_SOURCE_DIR}/ui")
set(UI_DIST "${CMAKE_CURRENT_BINARY_DIR}/ui")
file(GLOB_RECURSE UI_SOURCES CONFIGURE_DEPENDS "${UI_DIR}/src/*" "${UI_DIR}/index.html" "${UI_DIR}/vite.config.ts" "${UI_DIR}/tsconfig.json")
if(FALCON_NPM)
    add_custom_command(
        OUTPUT "${UI_DIR}/node_modules/.falcon-installed"
        COMMAND "${FALCON_NPM}" install --no-audit --no-fund --loglevel=error
        COMMAND "${CMAKE_COMMAND}" -E touch "${UI_DIR}/node_modules/.falcon-installed"
        DEPENDS "${UI_DIR}/package.json"
        WORKING_DIRECTORY "${UI_DIR}"
        COMMENT "Installing the launcher UI's packages (npm)"
        VERBATIM)
    add_custom_command(
        OUTPUT "${UI_DIST}/index.html"
        COMMAND "${FALCON_NPM}" run build --silent -- --outDir "${UI_DIST}" --emptyOutDir
        DEPENDS "${UI_DIR}/node_modules/.falcon-installed" ${UI_SOURCES}
        WORKING_DIRECTORY "${UI_DIR}"
        COMMENT "Building the launcher UI (Vite, one html file)"
        VERBATIM)
    add_custom_target(falcon_launcher_ui DEPENDS "${UI_DIST}/index.html")
else()
    message(STATUS "launcher: npm not found, the launcher UI cannot be built")
    add_custom_target(falcon_launcher_ui COMMAND "${CMAKE_COMMAND}" -E false COMMENT "npm is needed to build the launcher UI")
endif()

# ---------------------------------------------------------------------------
# The executable. Its resources: the UI, the game client (always bundled, extracted by the first-run setup) and the WebView2 loader.
# ---------------------------------------------------------------------------
set(LAUNCHER_RC "${CMAKE_CURRENT_BINARY_DIR}/falcon_launcher.rc")
file(GENERATE OUTPUT "${LAUNCHER_RC}" CONTENT
"1 ICON \"${FALCON_ICO}\"
1 24 \"${CMAKE_CURRENT_SOURCE_DIR}/src/launcher.manifest\"
LAUNCHER_UI RCDATA \"${UI_DIST}/index.html\"
FALCON_CLIENT RCDATA \"$<TARGET_FILE:falcon_client>\"
WEBVIEW2_LOADER RCDATA \"${WV2_LOADER}\"

1 VERSIONINFO
FILEVERSION ${PROJECT_VERSION_MAJOR},${PROJECT_VERSION_MINOR},${PROJECT_VERSION_PATCH},0
PRODUCTVERSION ${PROJECT_VERSION_MAJOR},${PROJECT_VERSION_MINOR},${PROJECT_VERSION_PATCH},0
{
    BLOCK \"StringFileInfo\"
    {
        BLOCK \"040904B0\"
        {
            VALUE \"FileDescription\", \"Falcon Launcher\"
            VALUE \"ProductName\", \"Falcon Launcher\"
            VALUE \"InternalName\", \"FalconLauncher\"
            VALUE \"OriginalFilename\", \"FalconLauncher.exe\"
            VALUE \"FileVersion\", \"${PROJECT_VERSION}\"
            VALUE \"ProductVersion\", \"${PROJECT_VERSION}\"
        }
    }
    BLOCK \"VarFileInfo\"
    {
        VALUE \"Translation\", 0x409, 1200
    }
}
")
set_source_files_properties("${LAUNCHER_RC}" PROPERTIES GENERATED TRUE
    OBJECT_DEPENDS "${UI_DIST}/index.html;${CMAKE_BINARY_DIR}/Falcon.exe;${WV2_LOADER};${FALCON_ICO}")
# (Its own target so windres gets no -I flags, as with the game's resource file.)
add_library(falcon_launcher_rc OBJECT EXCLUDE_FROM_ALL "${LAUNCHER_RC}")
add_dependencies(falcon_launcher_rc falcon_launcher_ui falcon_client falcon_icon)

add_executable(falcon_launcher WIN32 EXCLUDE_FROM_ALL
    src/main.cpp
    src/webview_host.cpp
    $<TARGET_OBJECTS:falcon_launcher_rc>)
target_include_directories(falcon_launcher SYSTEM PRIVATE "${WV2_INCLUDE}")
target_link_libraries(falcon_launcher PRIVATE falcon_launcher_core dwmapi gdi32 user32 comctl32)
set_target_properties(falcon_launcher PROPERTIES OUTPUT_NAME "FalconLauncher")
if(MINGW)
    target_link_options(falcon_launcher PRIVATE -static)
endif()
if(NOT MSVC)
    target_link_options(falcon_launcher PRIVATE $<$<CONFIG:Release>:-s>)
endif()

# dist-launcher/ holds the finished launcher (the game's dist/ is emptied on every game build, so it cannot live there).
set(FALCON_LAUNCHER_DIST "${CMAKE_SOURCE_DIR}/dist-launcher" CACHE PATH "Where the finished launcher is placed")
add_custom_command(TARGET falcon_launcher POST_BUILD
    COMMAND "${CMAKE_COMMAND}" -E make_directory "${FALCON_LAUNCHER_DIST}"
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different "$<TARGET_FILE:falcon_launcher>" "${FALCON_LAUNCHER_DIST}/FalconLauncher.exe"
    COMMENT "Placing FalconLauncher.exe in dist-launcher/"
    VERBATIM)

# ---------------------------------------------------------------------------
# Tests of the logic (no window, no network: the HTTP client is faked).
# ---------------------------------------------------------------------------
if(FALCON_BUILD_TESTS)
    add_executable(falcon_launcher_tests EXCLUDE_FROM_ALL tests/launcher_tests.cpp)
    target_link_libraries(falcon_launcher_tests PRIVATE falcon_launcher_core)
    add_test(NAME falcon_launcher_tests COMMAND falcon_launcher_tests)
    if(TARGET check)
        add_dependencies(check falcon_launcher_tests)
    endif()
endif()
