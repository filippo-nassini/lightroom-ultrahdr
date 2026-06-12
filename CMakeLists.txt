cmake_minimum_required(VERSION 3.15)
project(uhdrtool VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Self-contained binary: static-link the MSVC runtime (/MT). libultrahdr and
# libjpeg-turbo build /MT; matching here avoids the LNK4098 CRT-mismatch warning
# and means the shipped .exe needs no VC++ redistributable.
if(MSVC)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

# --- libultrahdr (fetched at configure time) ----------------------------------
# Pulled from upstream and pinned to an exact commit, then built from source as
# part of our build. libultrahdr in turn fetches its own libjpeg-turbo dependency
# (UHDR_BUILD_DEPS), so nothing third-party is committed to this repo — the build
# already needed the network for libjpeg-turbo regardless.
#
# Prerequisites: git (for the fetch) and NASM (for libjpeg-turbo's SIMD).
#
# Options are pre-seeded BEFORE the source is made available; libultrahdr's CMake
# uses option_if_not_defined(), which honors an already-set variable.
include(FetchContent)

set(BUILD_SHARED_LIBS OFF)
set(UHDR_BUILD_DEPS ON)        # build libjpeg-turbo from source (requires NASM)
set(UHDR_BUILD_EXAMPLES OFF)
set(UHDR_BUILD_TESTS OFF)
set(UHDR_BUILD_BENCHMARK OFF)
set(UHDR_ENABLE_INSTALL OFF)

FetchContent_Declare(
    libultrahdr
    GIT_REPOSITORY https://github.com/google/libultrahdr.git
    # v1.4.0-18-g13a058f — keep in sync with licenses/NOTICE.md.
    GIT_TAG        13a058f452d846e43d4691f6885eeeaa8b0ea8d0
    GIT_SHALLOW    FALSE
)
FetchContent_MakeAvailable(libultrahdr)

# --- uhdrtool -----------------------------------------------------------------
add_executable(uhdrtool src/main.cpp src/tiff_reader.cpp src/encoder.cpp)
target_include_directories(uhdrtool PRIVATE ${libultrahdr_SOURCE_DIR})

# Link-target name: uhdr, except MSVC-static where libultrahdr exports uhdr-static.
if(TARGET uhdr-static)
    target_link_libraries(uhdrtool PRIVATE uhdr-static)
elseif(TARGET uhdr)
    target_link_libraries(uhdrtool PRIVATE uhdr)
else()
    message(FATAL_ERROR "libultrahdr exported neither 'uhdr' nor 'uhdr-static'")
endif()

# --- Drop the built binary into the .lrplugin bundle --------------------------
# The plugin (BinaryLocator.lua) loads <plugin>/bin/<win|mac>/uhdrtool[.exe].
# Copy the freshly-built exe there after every build so the bundle and the build
# never drift (you no longer hand-copy after a rebuild). The CI packaging step
# zips this same bundle for the Release.
set(PLUGIN_BUNDLE "${CMAKE_CURRENT_SOURCE_DIR}/lua/lightroom-hdr.lrplugin")
if(WIN32)
    set(PLUGIN_BIN_DIR "${PLUGIN_BUNDLE}/bin/win")
elseif(APPLE)
    set(PLUGIN_BIN_DIR "${PLUGIN_BUNDLE}/bin/mac")
endif()
if(DEFINED PLUGIN_BIN_DIR)
    add_custom_command(TARGET uhdrtool POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory "${PLUGIN_BIN_DIR}"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "$<TARGET_FILE:uhdrtool>" "${PLUGIN_BIN_DIR}/"
        COMMENT "Copying uhdrtool into the .lrplugin bundle (${PLUGIN_BIN_DIR})")
endif()
