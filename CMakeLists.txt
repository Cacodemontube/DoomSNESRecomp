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
    src/doom_music.c
    src/doom_episodes.c
    src/doom_rotations.cpp
    src/doom_rotation_mod.c
    src/doom_crosshair.c
    src/doom_infighting.c
)

option(DOOM_DIAGNOSTIC_BUILD "Windows build that automatically writes a performance report ZIP" OFF)
option(DOOM_SNESED_TEST "Enable the DoomSNESed test launcher and native level patches" OFF)
if(DOOM_SNESED_TEST)
    add_library(DoomSNESAudio SHARED src/doom_audio_preview.c
        snesrecomp/runner/src/snes/apu.c snesrecomp/runner/src/snes/spc.c
        snesrecomp/runner/src/snes/dsp.c snesrecomp/runner/src/snes/dsp_shadow.c
        snesrecomp/runner/src/snes/audio_shadow.c)
    target_include_directories(DoomSNESAudio PRIVATE snesrecomp/runner/src)
    set_target_properties(DoomSNESAudio PROPERTIES PREFIX "" OUTPUT_NAME "DoomSNESAudio")
    add_dependencies(DoomSNESRecomp DoomSNESAudio)
    if(MINGW)
        target_link_options(DoomSNESAudio PRIVATE -static-libgcc)
    endif()
    target_sources(DoomSNESRecomp PRIVATE src/doom_editor.c src/doom_editor_patch.c)
    set_property(SOURCE src/main.c src/doom_editor.c APPEND PROPERTY
        COMPILE_DEFINITIONS DOOM_SNESED_TEST=1)
endif()
option(DOOM_ASYNC_PRESENTATION "Enable isolated native-view camera worker by default" ON)
if(DOOM_ASYNC_PRESENTATION)
    target_compile_definitions(DoomSNESRecomp PRIVATE DOOM_ASYNC_PRESENTATION_DEFAULT=1)
endif()
option(DOOM_ENABLE_IPO "Enable cross-file optimization for the Release game executable" OFF)
if(DOOM_ENABLE_IPO)
    include(CheckIPOSupported)
    check_ipo_supported(RESULT _doom_ipo_supported OUTPUT _doom_ipo_error LANGUAGES C CXX)
    if(NOT _doom_ipo_supported)
        message(FATAL_ERROR "DOOM_ENABLE_IPO is unsupported by this toolchain: ${_doom_ipo_error}")
    endif()
    set_property(TARGET DoomSNESRecomp PROPERTY INTERPROCEDURAL_OPTIMIZATION_RELEASE TRUE)
endif()
if(DOOM_DIAGNOSTIC_BUILD)
    if(NOT WIN32)
        message(FATAL_ERROR "DOOM_DIAGNOSTIC_BUILD currently requires Windows")
    endif()
    target_sources(DoomSNESRecomp PRIVATE src/doom_diagnostic.c)
    target_compile_definitions(DoomSNESRecomp PRIVATE DOOM_DIAGNOSTIC_BUILD=1)
    target_link_libraries(DoomSNESRecomp PRIVATE shell32 advapi32)
endif()

if(WIN32)
    option(DOOM_WINDOWS_CONSOLE "Open a Windows console beside the game" OFF)
    if(NOT DOOM_WINDOWS_CONSOLE)
        set_target_properties(DoomSNESRecomp PROPERTIES WIN32_EXECUTABLE TRUE)
        if(MSVC)
            target_link_options(DoomSNESRecomp PRIVATE /ENTRY:mainCRTStartup)
        endif()
    endif()
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
set(DOOM_GENERATED_DIR "${CMAKE_SOURCE_DIR}/src/gen" CACHE PATH
    "Generated code directory (may be outside the source tree)")
snesrecomp_target_generated_code(DoomSNESRecomp ${DOOM_GENERATED_DIR})

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
target_compile_definitions(DoomSNESRecomp PRIVATE DOOM_LAUNCHER=1)
if(DOOM_SNESED_TEST)
    set_property(SOURCE "${RECOMP_UI_ROOT}/src/common/backends/imgui/launcher_imgui.cpp"
        APPEND PROPERTY COMPILE_DEFINITIONS DOOM_SNESED_TEST=1)
endif()
add_custom_command(TARGET DoomSNESRecomp POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_SOURCE_DIR}/logo1.png"
            "$<TARGET_FILE_DIR:DoomSNESRecomp>/assets/img/logo1.png")

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
    add_executable(doom_radius_filter_test tests/test_doom_radius_filter.c
        ${SNESRECOMP_ROOT}/runner/src/snes/superfx.c)
    target_include_directories(doom_radius_filter_test PRIVATE ${SNESRECOMP_ROOT}/runner/src)
    if(EXISTS "${CMAKE_SOURCE_DIR}/Doom (USA).sfc")
        add_test(NAME doom_radius_filter COMMAND doom_radius_filter_test "${CMAKE_SOURCE_DIR}/Doom (USA).sfc")
    endif()
    add_executable(doom_radius_range_test tests/test_doom_radius_range.c
        ${SNESRECOMP_ROOT}/runner/src/snes/superfx.c)
    target_include_directories(doom_radius_range_test PRIVATE ${SNESRECOMP_ROOT}/runner/src)
    if(EXISTS "${CMAKE_SOURCE_DIR}/Doom (USA).sfc")
        add_test(NAME doom_radius_range COMMAND doom_radius_range_test "${CMAKE_SOURCE_DIR}/Doom (USA).sfc")
    endif()
    add_executable(doom_animation_queues_test tests/test_doom_animation_queues.c
        ${SNESRECOMP_ROOT}/runner/src/snes/superfx.c)
    target_include_directories(doom_animation_queues_test PRIVATE ${SNESRECOMP_ROOT}/runner/src)
    if(EXISTS "${CMAKE_SOURCE_DIR}/Doom (USA).sfc")
        add_test(NAME doom_animation_queues COMMAND doom_animation_queues_test "${CMAKE_SOURCE_DIR}/Doom (USA).sfc")
    endif()
    add_executable(doom_animation_iterator_test tests/test_doom_animation_iterator.c
        ${SNESRECOMP_ROOT}/runner/src/snes/superfx.c)
    target_include_directories(doom_animation_iterator_test PRIVATE ${SNESRECOMP_ROOT}/runner/src)
    if(EXISTS "${CMAKE_SOURCE_DIR}/Doom (USA).sfc")
        add_test(NAME doom_animation_iterator COMMAND doom_animation_iterator_test "${CMAKE_SOURCE_DIR}/Doom (USA).sfc")
    endif()
    add_executable(doom_movable_lifecycle_test tests/test_doom_movable_lifecycle.c
        ${SNESRECOMP_ROOT}/runner/src/snes/superfx.c)
    target_include_directories(doom_movable_lifecycle_test PRIVATE ${SNESRECOMP_ROOT}/runner/src)
    if(EXISTS "${CMAKE_SOURCE_DIR}/Doom (USA).sfc")
        add_test(NAME doom_movable_lifecycle COMMAND doom_movable_lifecycle_test "${CMAKE_SOURCE_DIR}/Doom (USA).sfc")
    endif()
    add_executable(doom_ammo_pickup_test tests/test_doom_ammo_pickup.c
        ${SNESRECOMP_ROOT}/runner/src/snes/superfx.c)
    target_include_directories(doom_ammo_pickup_test PRIVATE ${SNESRECOMP_ROOT}/runner/src)
    if(EXISTS "${CMAKE_SOURCE_DIR}/Doom (USA).sfc")
        add_test(NAME doom_ammo_pickup COMMAND doom_ammo_pickup_test "${CMAKE_SOURCE_DIR}/Doom (USA).sfc")
    endif()
    add_executable(doom_item_gate_test tests/test_doom_item_gate.c
        ${SNESRECOMP_ROOT}/runner/src/snes/superfx.c)
    target_include_directories(doom_item_gate_test PRIVATE ${SNESRECOMP_ROOT}/runner/src)
    if(EXISTS "${CMAKE_SOURCE_DIR}/Doom (USA).sfc")
        add_test(NAME doom_item_gate COMMAND doom_item_gate_test "${CMAKE_SOURCE_DIR}/Doom (USA).sfc")
    endif()
    add_executable(doom_object_contact_test tests/test_doom_object_contact.c
        ${SNESRECOMP_ROOT}/runner/src/snes/superfx.c)
    target_include_directories(doom_object_contact_test PRIVATE ${SNESRECOMP_ROOT}/runner/src)
    if(EXISTS "${CMAKE_SOURCE_DIR}/Doom (USA).sfc")
        add_test(NAME doom_object_contact COMMAND doom_object_contact_test "${CMAKE_SOURCE_DIR}/Doom (USA).sfc")
    endif()
    add_executable(doom_projectile_clearance_test tests/test_doom_projectile_clearance.c
        ${SNESRECOMP_ROOT}/runner/src/snes/superfx.c)
    target_include_directories(doom_projectile_clearance_test PRIVATE ${SNESRECOMP_ROOT}/runner/src)
    if(EXISTS "${CMAKE_SOURCE_DIR}/Doom (USA).sfc")
        add_test(NAME doom_projectile_clearance COMMAND doom_projectile_clearance_test "${CMAKE_SOURCE_DIR}/Doom (USA).sfc")
    endif()
    add_executable(doom_enemy_clearance_test tests/test_doom_enemy_clearance.c
        ${SNESRECOMP_ROOT}/runner/src/snes/superfx.c)
    target_include_directories(doom_enemy_clearance_test PRIVATE ${SNESRECOMP_ROOT}/runner/src)
    if(EXISTS "${CMAKE_SOURCE_DIR}/Doom (USA).sfc")
        add_test(NAME doom_enemy_clearance COMMAND doom_enemy_clearance_test "${CMAKE_SOURCE_DIR}/Doom (USA).sfc")
    endif()
    add_executable(doom_player_pipeline_test tests/test_doom_player_pipeline.c
        ${SNESRECOMP_ROOT}/runner/src/snes/superfx.c)
    target_include_directories(doom_player_pipeline_test PRIVATE ${SNESRECOMP_ROOT}/runner/src)
    if(EXISTS "${CMAKE_SOURCE_DIR}/Doom (USA).sfc")
        add_test(NAME doom_player_pipeline COMMAND doom_player_pipeline_test "${CMAKE_SOURCE_DIR}/Doom (USA).sfc")
    endif()
    add_executable(doom_bsp_sector_test tests/test_doom_bsp_sector.c
        ${SNESRECOMP_ROOT}/runner/src/snes/superfx.c)
    target_include_directories(doom_bsp_sector_test PRIVATE ${SNESRECOMP_ROOT}/runner/src)
    if(EXISTS "${CMAKE_SOURCE_DIR}/Doom (USA).sfc")
        add_test(NAME doom_bsp_sector COMMAND doom_bsp_sector_test "${CMAKE_SOURCE_DIR}/Doom (USA).sfc")
    endif()
    add_executable(doom_sector_links_test tests/test_doom_sector_links.c
        ${SNESRECOMP_ROOT}/runner/src/snes/superfx.c)
    target_include_directories(doom_sector_links_test PRIVATE ${SNESRECOMP_ROOT}/runner/src)
    if(EXISTS "${CMAKE_SOURCE_DIR}/Doom (USA).sfc")
        add_test(NAME doom_sector_links COMMAND doom_sector_links_test "${CMAKE_SOURCE_DIR}/Doom (USA).sfc")
    endif()
    add_executable(doom_slide_test tests/test_doom_slide.c
        ${SNESRECOMP_ROOT}/runner/src/snes/superfx.c)
    target_include_directories(doom_slide_test PRIVATE ${SNESRECOMP_ROOT}/runner/src)
    if(EXISTS "${CMAKE_SOURCE_DIR}/Doom (USA).sfc")
        add_test(NAME doom_slide COMMAND doom_slide_test "${CMAKE_SOURCE_DIR}/Doom (USA).sfc")
    endif()
    add_executable(doom_clipped_move_test tests/test_doom_clipped_move.c
        ${SNESRECOMP_ROOT}/runner/src/snes/superfx.c)
    target_include_directories(doom_clipped_move_test PRIVATE ${SNESRECOMP_ROOT}/runner/src)
    if(EXISTS "${CMAKE_SOURCE_DIR}/Doom (USA).sfc")
        add_test(NAME doom_clipped_move COMMAND doom_clipped_move_test "${CMAKE_SOURCE_DIR}/Doom (USA).sfc")
    endif()
    add_executable(doom_line_side_test tests/test_doom_line_side.c
        ${SNESRECOMP_ROOT}/runner/src/snes/superfx.c)
    target_include_directories(doom_line_side_test PRIVATE ${SNESRECOMP_ROOT}/runner/src)
    if(EXISTS "${CMAKE_SOURCE_DIR}/Doom (USA).sfc")
        add_test(NAME doom_line_side COMMAND doom_line_side_test "${CMAKE_SOURCE_DIR}/Doom (USA).sfc")
    endif()
    add_executable(doom_player_clearance_test tests/test_doom_player_clearance.c
        ${SNESRECOMP_ROOT}/runner/src/snes/superfx.c)
    target_include_directories(doom_player_clearance_test PRIVATE ${SNESRECOMP_ROOT}/runner/src)
    if(EXISTS "${CMAKE_SOURCE_DIR}/Doom (USA).sfc")
        add_test(NAME doom_player_clearance COMMAND doom_player_clearance_test "${CMAKE_SOURCE_DIR}/Doom (USA).sfc")
    endif()
    add_executable(doom_object_filter_test tests/test_doom_object_filter.c
        ${SNESRECOMP_ROOT}/runner/src/snes/superfx.c)
    target_include_directories(doom_object_filter_test PRIVATE ${SNESRECOMP_ROOT}/runner/src)
    if(EXISTS "${CMAKE_SOURCE_DIR}/Doom (USA).sfc")
        add_test(NAME doom_object_filter COMMAND doom_object_filter_test "${CMAKE_SOURCE_DIR}/Doom (USA).sfc")
    endif()
    add_executable(doom_move_sectors_test tests/test_doom_move_sectors.c
        ${SNESRECOMP_ROOT}/runner/src/snes/superfx.c)
    target_include_directories(doom_move_sectors_test PRIVATE ${SNESRECOMP_ROOT}/runner/src)
    if(EXISTS "${CMAKE_SOURCE_DIR}/Doom (USA).sfc")
        add_test(NAME doom_move_sectors COMMAND doom_move_sectors_test "${CMAKE_SOURCE_DIR}/Doom (USA).sfc")
    endif()
    add_executable(doom_move_candidates_test tests/test_doom_move_candidates.c
        ${SNESRECOMP_ROOT}/runner/src/snes/superfx.c)
    target_include_directories(doom_move_candidates_test PRIVATE ${SNESRECOMP_ROOT}/runner/src)
    if(EXISTS "${CMAKE_SOURCE_DIR}/Doom (USA).sfc")
        add_test(NAME doom_move_candidates COMMAND doom_move_candidates_test "${CMAKE_SOURCE_DIR}/Doom (USA).sfc")
    endif()
    add_executable(doom_move_coordinates_test tests/test_doom_move_coordinates.c
        ${SNESRECOMP_ROOT}/runner/src/snes/superfx.c)
    target_include_directories(doom_move_coordinates_test PRIVATE ${SNESRECOMP_ROOT}/runner/src)
    if(EXISTS "${CMAKE_SOURCE_DIR}/Doom (USA).sfc")
        add_test(NAME doom_move_coordinates COMMAND doom_move_coordinates_test "${CMAKE_SOURCE_DIR}/Doom (USA).sfc")
    endif()
    set(DOOM_REFERENCE_SOURCE_DIR "C:/projects/doom-fx-reference/source" CACHE PATH
        "Original DOOM-FX assembly for source parity tests")
    add_executable(doom_object_angle_test tests/test_doom_object_angle.c
        ${SNESRECOMP_ROOT}/runner/src/snes/superfx.c)
    target_include_directories(doom_object_angle_test PRIVATE ${SNESRECOMP_ROOT}/runner/src)
    if(EXISTS "${DOOM_REFERENCE_SOURCE_DIR}/sincos.a" AND EXISTS "${DOOM_REFERENCE_SOURCE_DIR}/InverseTable.bin")
        add_test(NAME doom_object_angle COMMAND doom_object_angle_test
            "${CMAKE_SOURCE_DIR}/Doom (USA).sfc" "${DOOM_REFERENCE_SOURCE_DIR}/InverseTable.bin"
            "${DOOM_REFERENCE_SOURCE_DIR}/sincos.a")
    else()
        message(STATUS "Object-angle source oracle not registered: original assembly tables unavailable")
    endif()
    add_executable(doom_player_velocity_test tests/test_doom_player_velocity.c
        ${SNESRECOMP_ROOT}/runner/src/snes/superfx.c)
    target_include_directories(doom_player_velocity_test PRIVATE ${SNESRECOMP_ROOT}/runner/src)
    if(EXISTS "${DOOM_REFERENCE_SOURCE_DIR}/sincos.a")
        add_test(NAME doom_player_velocity COMMAND doom_player_velocity_test
            "${CMAKE_SOURCE_DIR}/Doom (USA).sfc" "${DOOM_REFERENCE_SOURCE_DIR}/sincos.a")
    else()
        message(STATUS "Physics source oracle not registered: set DOOM_REFERENCE_SOURCE_DIR to the original assembly source")
    endif()
    add_executable(doom_player_gate_test tests/test_doom_player_gate.c
        ${SNESRECOMP_ROOT}/runner/src/snes/superfx.c)
    target_include_directories(doom_player_gate_test PRIVATE ${SNESRECOMP_ROOT}/runner/src)
    if(EXISTS "${CMAKE_SOURCE_DIR}/Doom (USA).sfc")
        add_test(NAME doom_player_gate COMMAND doom_player_gate_test "${CMAKE_SOURCE_DIR}/Doom (USA).sfc")
    endif()
    add_executable(doom_gravity_test tests/test_doom_gravity.c
        ${SNESRECOMP_ROOT}/runner/src/snes/superfx.c)
    target_include_directories(doom_gravity_test PRIVATE ${SNESRECOMP_ROOT}/runner/src)
    if(EXISTS "${CMAKE_SOURCE_DIR}/Doom (USA).sfc")
        add_test(NAME doom_gravity COMMAND doom_gravity_test "${CMAKE_SOURCE_DIR}/Doom (USA).sfc")
    endif()
    add_executable(doom_episodes_test tests/test_doom_episodes.c)
    target_include_directories(doom_episodes_test PRIVATE src ${SNESRECOMP_RUNNER_INCLUDE_DIRS})
    if(EXISTS "${CMAKE_SOURCE_DIR}/Doom (USA).sfc")
        add_test(NAME doom_episodes COMMAND doom_episodes_test "${CMAKE_SOURCE_DIR}/Doom (USA).sfc")
    endif()
    add_executable(doom_cheats_test tests/test_doom_cheats.c
        ${SNESRECOMP_ROOT}/runner/src/snes/superfx.c)
    target_include_directories(doom_cheats_test PRIVATE src
        ${SNESRECOMP_RUNNER_INCLUDE_DIRS} ${SNESRECOMP_ROOT}/runner/src/desktop)
    snesrecomp_target_sdl(doom_cheats_test)
    add_executable(doom_infighting_test tests/test_doom_infighting.c
        ${SNESRECOMP_ROOT}/runner/src/snes/superfx.c)
    target_include_directories(doom_infighting_test PRIVATE src ${SNESRECOMP_RUNNER_INCLUDE_DIRS})
    if(NOT MSVC)
        target_link_libraries(doom_infighting_test PRIVATE m)
    endif()
    if(EXISTS "${CMAKE_SOURCE_DIR}/Doom (USA).sfc")
        add_test(NAME doom_infighting COMMAND doom_infighting_test "${CMAKE_SOURCE_DIR}/Doom (USA).sfc")
    endif()
    if(EXISTS "${CMAKE_SOURCE_DIR}/Doom (USA).sfc")
        add_test(NAME doom_cheats COMMAND doom_cheats_test "${CMAKE_SOURCE_DIR}/Doom (USA).sfc")
    endif()
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
    add_executable(doom_rotations_test tests/doom_rotations_test.cpp src/doom_rotations.cpp)
    add_executable(doom_crosshair_test tests/test_doom_crosshair.c)
    target_include_directories(doom_crosshair_test PRIVATE src)
    add_test(NAME doom_crosshair COMMAND doom_crosshair_test)
    target_include_directories(doom_rotations_test PRIVATE src)
    set(DOOM_TEST_WAD "" CACHE FILEPATH "Optional user-owned Doom/Ultimate Doom IWAD for rotation tests")
    if(DOOM_TEST_WAD)
        add_test(NAME doom_rotations COMMAND doom_rotations_test "${DOOM_TEST_WAD}" "${CMAKE_BINARY_DIR}/rotation-test/sprites")
    endif()
    add_executable(doom_resolution_test tests/test_doom_resolution.c src/doom_rotations.cpp)
    target_include_directories(doom_resolution_test PRIVATE src)
    if(NOT MSVC)
        target_link_libraries(doom_resolution_test PRIVATE m)
    endif()
    add_test(NAME doom_resolution COMMAND doom_resolution_test)
    if(DOOM_TEST_WAD)
        add_test(NAME doom_rotation_capture COMMAND doom_resolution_test "${DOOM_TEST_WAD}" "${CMAKE_BINARY_DIR}/rotation-capture-test/sprites")
    endif()
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
    add_executable(superfx_pixel_flush_test tests/test_superfx_pixel_flush.c)
    target_include_directories(superfx_pixel_flush_test PRIVATE ${SNESRECOMP_ROOT}/runner/src)
    add_test(NAME superfx_pixel_flush COMMAND superfx_pixel_flush_test)
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

    # Manual real-driver check; headless CI need not provide OpenGL 3.3.
    add_executable(glsl_storage_probe EXCLUDE_FROM_ALL tests/glsl_storage_probe.c
        ${SNESRECOMP_ROOT}/runner/src/util.c
        ${SNESRECOMP_ROOT}/third_party/gl_core/gl_core_3_1.c)
    target_include_directories(glsl_storage_probe PRIVATE ${SNESRECOMP_RUNNER_INCLUDE_DIRS})
    snesrecomp_target_sdl(glsl_storage_probe)
    snesrecomp_target_glsl_shader(glsl_storage_probe)

    add_executable(doom_presentation_test tests/test_doom_presentation.c
        src/doom_video.c src/doom_presentation.c src/doom_rotations.cpp)
    target_include_directories(doom_presentation_test PRIVATE src
        ${SNESRECOMP_RUNNER_INCLUDE_DIRS} ${SNESRECOMP_ROOT}/runner/src/desktop)
    snesrecomp_target_sdl(doom_presentation_test)
    if(NOT MSVC)
        target_link_libraries(doom_presentation_test PRIVATE m)
    endif()
    add_test(NAME doom_presentation COMMAND doom_presentation_test)

    add_executable(doom_ppu_state_test tests/test_doom_ppu_state.c
        snesrecomp/runner/src/snes/ppu_legacy.c)
    target_include_directories(doom_ppu_state_test PRIVATE src snesrecomp/runner/src)
    target_compile_definitions(doom_ppu_state_test PRIVATE SNESRECOMP_REVERSE_DEBUG=0)
    if(NOT MSVC)
        target_link_libraries(doom_ppu_state_test PRIVATE m)
    endif()
    add_test(NAME doom_ppu_state COMMAND doom_ppu_state_test)

    add_executable(doom_renderer_state_test tests/doom_renderer_state_test.c src/doom_rotations.cpp)
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
        src/doom_rotations.cpp
        ${SNESRECOMP_ROOT}/runner/src/snes/superfx.c)
    target_include_directories(doom_renderer_replay_probe PRIVATE
        ${SNESRECOMP_ROOT}/runner/src)
    target_compile_definitions(doom_renderer_replay_probe PRIVATE DOOM_REPLAY_PARALLEL=1)
    snesrecomp_target_sdl(doom_renderer_replay_probe)
    if(NOT MSVC)
        target_link_libraries(doom_renderer_replay_probe PRIVATE m)
    endif()
endif()
