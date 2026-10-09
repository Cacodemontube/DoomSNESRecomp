cmake_minimum_required(VERSION 3.20)
project(DoomSNESRecomp C CXX)

set(CMAKE_C_STANDARD 11)

# Release pin for lobby matching. Release builds stamp the VERSION file so two
# peers can prove they are the same build; everything else stamps "dev".
file(READ "${CMAKE_CURRENT_SOURCE_DIR}/VERSION" _GAME_VER_RAW)
string(STRIP "${_GAME_VER_RAW}" GAME_RELEASE_VERSION)
set(SNESRECOMP_BUILD_VERSION "" CACHE STRING
    "Lobby release pin (empty = Release uses VERSION, else dev)")
if(SNESRECOMP_BUILD_VERSION STREQUAL "")
    get_property(_is_multi GLOBAL PROPERTY GENERATOR_IS_MULTI_CONFIG)
    if(_is_multi)
        set(SNESRECOMP_BUILD_VERSION "$<IF:$<CONFIG:Release>,${GAME_RELEASE_VERSION},dev>")
    elseif(CMAKE_BUILD_TYPE STREQUAL "Release")
        set(SNESRECOMP_BUILD_VERSION "${GAME_RELEASE_VERSION}")
    else()
        set(SNESRECOMP_BUILD_VERSION "dev")
    endif()
endif()

set(SNESRECOMP_ROOT "${CMAKE_SOURCE_DIR}/snesrecomp" CACHE PATH
    "Path to the snesrecomp checkout used by this game")
if(NOT EXISTS "${SNESRECOMP_ROOT}/runner/runner.cmake")
    message(FATAL_ERROR
        "snesrecomp submodule missing at ${SNESRECOMP_ROOT}.\n"
        "Run: git submodule update --init --recursive")
endif()
# Mod packages are part of every project, not an option. The launcher's Mods
# page, the .snesmod loader, the trusted-plugin registry and netplay's mod-set
# gate all hang off this one switch; a build without it cannot exchange mods
# with a peer that has them, and the framework says so at every lobby init.
# Must precede the include: runner.cmake reads it to add the loader sources.
set(SNESRECOMP_ENABLE_MODS ON CACHE BOOL
    "Mod package loader + trusted plugins (required for every title)" FORCE)
include(${SNESRECOMP_ROOT}/runner/runner.cmake)

add_executable(DoomSNESRecomp
    ${SNESRECOMP_RUNNER_SOURCES}
    src/main.c
    src/host_contract.c
    src/game_rtl.c
    src/gen_stubs.c
    src/doom_video.c
    src/doom_renderer.c
    src/doom_presentation.c
    src/doom_debug.c
    src/doom_input.c
    src/doom_cheats.c
    src/doom_episodes.c
)

if(WIN32)
    enable_language(RC)
    target_sources(DoomSNESRecomp PRIVATE assets/windows/doom.rc)
    set_source_files_properties(assets/windows/doom.rc PROPERTIES
        OBJECT_DEPENDS "${CMAKE_SOURCE_DIR}/assets/windows/doom.ico")
    target_include_directories(DoomSNESRecomp PRIVATE assets/windows)
endif()

# ROM identity. rom_identity.txt is the one place the digests live: the build
# turns it into snesrecomp_rom_identity.h (SNESRECOMP_ROM_EXPECTED_SHA256 and
# friends) for src/main.c, and tools/regen.sh, scripts/package_release.sh and
# the release workflow read the same file. A revision bump is one edit there.
snesrecomp_rom_identity(DoomSNESRecomp ${CMAKE_SOURCE_DIR}/rom_identity.txt)

# Generated recompiler output. Not committed: it is derived from a copyrighted
# ROM, so every developer regenerates it locally from their own copy -- and a
# release does not ship it either. With -DSNESRECOMP_SETUP_HOST=ON and an empty
# src/gen/, this builds the SETUP HOST instead: the same executable minus the
# recompiled code, whose only path is the launcher's Generate & rebuild wizard.
# That is what release zips contain; the player's machine produces the
# playable binary from their own ROM. Without the option an empty src/gen/ is
# still a hard error (the framework owns the message).
snesrecomp_target_generated_code(DoomSNESRecomp ${CMAKE_SOURCE_DIR}/src/gen)

# The mod catalog. This names WHERE the packages are; the framework owns the
# layout they are staged into beside the executable, and stages them on every
# build OF THE EXECUTABLE -- including the launcher's own Generate & rebuild on
# a player's machine, which builds only this target and would skip a sibling
# ALL target. mods/preloaded/packages/ holds reviewed, default-disabled
# packages; players install .snesmod archives through the launcher.
# snesrecomp/docs/MOD_PACKAGES.md.
#
# It used to be `snesrecomp_target_stage_dir(... ${CMAKE_SOURCE_DIR}/mods mods)`
# -- the destination spelled per title, which is how three ports in the same
# workspace ended up shipping an empty Mods page and nothing said so. Declaring
# it is now also enforced: a project with packages that never calls this fails
# to configure.
snesrecomp_target_mod_catalog(DoomSNESRecomp ${CMAKE_SOURCE_DIR}/mods/preloaded)
# Any other directory the game reads exe-relative (a translations/ table, ...)
# is staged with snesrecomp_target_stage_dir, never with a separate ALL target.

target_include_directories(DoomSNESRecomp PRIVATE
    ${SNESRECOMP_RUNNER_INCLUDE_DIRS}
    # common_rtl.c includes "config.h": the desktop key-map / Config contract
    # the framework host supplies (snesrecomp_target_desktop_host below).
    ${SNESRECOMP_ROOT}/runner/src/desktop
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/recomp
)
target_link_libraries(DoomSNESRecomp PRIVATE ${SNESRECOMP_RUNNER_LIBRARIES})
target_compile_definitions(DoomSNESRecomp PRIVATE
    SNES_GAME_VERSION="${SNESRECOMP_BUILD_VERSION}"
    SNES_GAME_TITLE="Doom"
)
snesrecomp_target_sdl(DoomSNESRecomp)

# The desktop host: launcher flow, ROM resolution, config.ini + [KeyMap],
# window + SDL/OpenGL presenters, audio, gamepads, the save-state browser and
# rewind filmstrip, the pacing clock, crash handlers and the post-mortem
# report. src/main.c is the shim that names this title and supplies its
# hooks; the host is the framework's and improves on a submodule pull.
# Add TIER2 once this port reads the interpreter-gap manifest after each run.
snesrecomp_target_desktop_host(DoomSNESRecomp)
# rom.cfg (the ROM the wizard set this project up from; git-ignored) is
# staged beside the executable when none is there yet, so the first launch
# opens on that dump rather than asking for one.
snesrecomp_target_rom_cache(DoomSNESRecomp)

# Shared Dear ImGui pre-boot launcher (ROM picker, verification, display /
# audio / input settings). Self-contained: it brings its own ImGui, GL link and
# staged assets, and defines RECOMP_LAUNCHER so src/main.c compiles its GUI path.
set(RECOMP_UI_ROOT "${CMAKE_SOURCE_DIR}/recomp-ui" CACHE PATH
    "Root directory of the recomp-ui launcher repo")
if(NOT EXISTS "${RECOMP_UI_ROOT}/recomp_ui.cmake")
    message(FATAL_ERROR
        "recomp-ui missing at ${RECOMP_UI_ROOT}.\n"
        "Run: git submodule update --init --recursive recomp-ui")
endif()
include(${RECOMP_UI_ROOT}/recomp_ui.cmake)
# BOXART stages launcher_assets/img/boxart.tga beside the exe; the launcher
# shows assets/img/boxart.tga by default. EXISTS-guarded in recomp_ui.cmake,
# so this configures cleanly before any art is fetched.
recomp_target_launcher_ui(DoomSNESRecomp CONSOLE snes
    BOXART "${CMAKE_SOURCE_DIR}/launcher_assets/img/boxart.tga")

# Generate & rebuild from inside the launcher: pick ROM -> recompile locally
# -> cmake --build -> relaunch. The implementation is the framework's; this
# project contributes no configuration (snesrecomp_codegen_host_autowire in
# src/main.c). recomp-ui/src is on the include path because the host speaks
# RecompLauncherCGameInfo.
target_sources(DoomSNESRecomp PRIVATE
    ${SNESRECOMP_ROOT}/host/snesrecomp_codegen_host.c)
target_include_directories(DoomSNESRecomp PRIVATE
    ${SNESRECOMP_ROOT}/host
    ${RECOMP_UI_ROOT}/src)

# Netplay is not built into this target.
# No multitap: two seats, one controller per port.

include(CTest)
if(BUILD_TESTING)
    add_executable(doom_episodes_test tests/test_doom_episodes.c)
    target_include_directories(doom_episodes_test PRIVATE src ${SNESRECOMP_RUNNER_INCLUDE_DIRS})
    add_test(NAME doom_episodes COMMAND doom_episodes_test "${CMAKE_SOURCE_DIR}/Doom (USA).sfc")
    add_executable(doom_cheats_test tests/test_doom_cheats.c
        ${SNESRECOMP_ROOT}/runner/src/snes/superfx.c)
    target_include_directories(doom_cheats_test PRIVATE src
        ${SNESRECOMP_RUNNER_INCLUDE_DIRS} ${SNESRECOMP_ROOT}/runner/src/desktop)
    snesrecomp_target_sdl(doom_cheats_test)
    add_test(NAME doom_cheats COMMAND doom_cheats_test "${CMAKE_SOURCE_DIR}/Doom (USA).sfc")
    add_executable(doom_input_test tests/test_doom_input.c)
    target_include_directories(doom_input_test PRIVATE src)
    if(NOT MSVC)
        target_link_libraries(doom_input_test PRIVATE m)
    endif()
    add_test(NAME doom_input COMMAND doom_input_test)
    add_executable(doom_input_adapter_test tests/test_doom_input_adapter.c
        ${SNESRECOMP_ROOT}/runner/src/snes/superfx.c)
    target_include_directories(doom_input_adapter_test PRIVATE src
        ${SNESRECOMP_RUNNER_INCLUDE_DIRS} ${SNESRECOMP_ROOT}/runner/src/desktop)
    snesrecomp_target_sdl(doom_input_adapter_test)
    if(NOT MSVC)
        target_link_libraries(doom_input_adapter_test PRIVATE m)
    endif()
    add_test(NAME doom_input_adapter COMMAND doom_input_adapter_test)
    add_executable(host_clock_test ${SNESRECOMP_ROOT}/tests/host/host_clock_test.c
        ${SNESRECOMP_ROOT}/runner/src/desktop/host_clock.c)
    target_include_directories(host_clock_test PRIVATE ${SNESRECOMP_ROOT}/runner/src/desktop)
    if(MSVC)
        target_compile_options(host_clock_test PRIVATE /UNDEBUG)
    else()
        target_compile_options(host_clock_test PRIVATE -UNDEBUG)
        target_link_libraries(host_clock_test PRIVATE m)
    endif()
    add_test(NAME host_clock COMMAND host_clock_test)
    add_executable(doom_resolution_test tests/test_doom_resolution.c)
    target_include_directories(doom_resolution_test PRIVATE src)
    if(NOT MSVC)
        target_link_libraries(doom_resolution_test PRIVATE m)
    endif()
    add_test(NAME doom_resolution COMMAND doom_resolution_test)
    add_executable(doom_automap_test tests/test_doom_automap.c)
    target_include_directories(doom_automap_test PRIVATE src)
    if(NOT MSVC)
        target_link_libraries(doom_automap_test PRIVATE m)
    endif()
    add_test(NAME doom_automap COMMAND doom_automap_test)
    add_executable(doom_weapon_test tests/test_doom_weapon.c)
    target_include_directories(doom_weapon_test PRIVATE src)
    if(NOT MSVC)
        target_link_libraries(doom_weapon_test PRIVATE m)
    endif()
    add_test(NAME doom_weapon COMMAND doom_weapon_test)
    add_executable(apu_frame_clock_test tests/test_apu_frame_clock.c)
    target_include_directories(apu_frame_clock_test PRIVATE ${SNESRECOMP_ROOT}/runner/src)
    add_test(NAME apu_frame_clock COMMAND apu_frame_clock_test)
    add_executable(superfx_replay_history_test tests/test_superfx_replay_history.c
        ${SNESRECOMP_ROOT}/runner/src/snes/superfx.c)
    target_include_directories(superfx_replay_history_test PRIVATE ${SNESRECOMP_ROOT}/runner/src)
    add_test(NAME superfx_replay_history COMMAND superfx_replay_history_test)
    add_executable(doom_video_test tests/test_doom_video.c src/doom_video.c)
    target_include_directories(doom_video_test PRIVATE src)
    if(NOT MSVC)
        target_link_libraries(doom_video_test PRIVATE m)
    endif()
    add_test(NAME doom_video COMMAND doom_video_test)

    add_executable(doom_projection_test tests/doom_projection_test.c)
    if(NOT MSVC)
        target_link_libraries(doom_projection_test PRIVATE m)
    endif()
    add_test(NAME doom_projection COMMAND doom_projection_test)

    add_executable(doom_shading_test tests/doom_shading_test.c)
    if(NOT MSVC)
        target_link_libraries(doom_shading_test PRIVATE m)
    endif()
    add_test(NAME doom_shading COMMAND doom_shading_test)

    add_executable(doom_presentation_test tests/test_doom_presentation.c
        src/doom_video.c src/doom_presentation.c)
    target_include_directories(doom_presentation_test PRIVATE src
        ${SNESRECOMP_RUNNER_INCLUDE_DIRS} ${SNESRECOMP_ROOT}/runner/src/desktop)
    snesrecomp_target_sdl(doom_presentation_test)
    if(NOT MSVC)
        target_link_libraries(doom_presentation_test PRIVATE m)
    endif()
    add_test(NAME doom_presentation COMMAND doom_presentation_test)

    add_executable(doom_renderer_state_test tests/doom_renderer_state_test.c)
    target_include_directories(doom_renderer_state_test PRIVATE
        ${SNESRECOMP_ROOT}/runner/src)
    if(NOT MSVC)
        target_link_libraries(doom_renderer_state_test PRIVATE m)
    endif()
    set(DOOM_TEST_ROM "" CACHE FILEPATH "Optional user-owned Doom USA ROM for renderer tests")
    if(DOOM_TEST_ROM)
        add_test(NAME doom_renderer_state COMMAND doom_renderer_state_test "${DOOM_TEST_ROM}")
    endif()

    add_executable(doom_renderer_replay_probe EXCLUDE_FROM_ALL
        tests/doom_renderer_replay_probe.c
        ${SNESRECOMP_ROOT}/runner/src/snes/superfx.c)
    target_include_directories(doom_renderer_replay_probe PRIVATE
        ${SNESRECOMP_ROOT}/runner/src)
    target_compile_definitions(doom_renderer_replay_probe PRIVATE DOOM_REPLAY_PARALLEL=1)
    snesrecomp_target_sdl(doom_renderer_replay_probe)
    if(NOT MSVC)
        target_link_libraries(doom_renderer_replay_probe PRIVATE m)
    endif()
endif()
