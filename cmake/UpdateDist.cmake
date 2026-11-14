# Post-build step: makes dist/ contain the freshly built game and nothing else.
#   cmake -DDIST_DIR=<dist> -DEXE=<built exe> -P UpdateDist.cmake
#
# The old contents are deleted first. A file that cannot be deleted because the game is still running from
# it is moved aside (renamed to *.old, which Windows allows), so the new exe can take its name at once; the
# *.old copy is deleted by the next build after the game has been closed. If even the rename fails, the file
# is left alone and named in a warning.

if(NOT DIST_DIR OR NOT EXE)
    message(FATAL_ERROR "UpdateDist.cmake needs -DDIST_DIR and -DEXE")
endif()

get_filename_component(exe_name "${EXE}" NAME)
set(locked_files)
set(moved_files)
if(EXISTS "${DIST_DIR}")
    file(GLOB old_files LIST_DIRECTORIES true "${DIST_DIR}/*")
    foreach(old IN LISTS old_files)
        execute_process(
            COMMAND "${CMAKE_COMMAND}" -E rm -rf "${old}"
            OUTPUT_QUIET ERROR_QUIET
        )
        if(EXISTS "${old}")
            # A running program cannot be deleted, but Windows lets it be renamed: move it aside so the new
            # build can take its name. The renamed copy is deleted by the next build once the game is closed.
            execute_process(
                COMMAND "${CMAKE_COMMAND}" -E rename "${old}" "${old}.old"
                RESULT_VARIABLE moved
                OUTPUT_QUIET ERROR_QUIET
            )
            if(moved EQUAL 0)
                list(APPEND moved_files "${old}.old")
            else()
                list(APPEND locked_files "${old}")
            endif()
        endif()
    endforeach()
endif()

if(EXISTS "${DIST_DIR}/${exe_name}")
    message(WARNING "dist/ was NOT updated: ${exe_name} is in use and could not be moved aside. Close it and build again.")
    return()
endif()

file(MAKE_DIRECTORY "${DIST_DIR}")
execute_process(
    COMMAND "${CMAKE_COMMAND}" -E copy "${EXE}" "${DIST_DIR}/${exe_name}"
    RESULT_VARIABLE copy_result
)
if(NOT copy_result EQUAL 0)
    message(FATAL_ERROR "could not copy ${exe_name} into ${DIST_DIR}")
endif()
message(STATUS "dist/ updated: ${DIST_DIR}/${exe_name}")
if(moved_files)
    list(JOIN moved_files ", " moved_text)
    message(STATUS "the running old version was moved aside (delete it after closing the game): ${moved_text}")
endif()
if(locked_files)
    list(JOIN locked_files ", " locked_text)
    message(WARNING "dist/ still holds files that are in use (old version, close the game to clear them): ${locked_text}")
endif()
