cmake_minimum_required(VERSION 3.20)
project(deceptus_console VERSION 0.1.0 LANGUAGES C)

set(CMAKE_C_STANDARD 90)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(DECEPTUS_DATA_DIR "${CMAKE_CURRENT_SOURCE_DIR}/assets/game/data" CACHE PATH "Deceptus game data (bundled by default)")
find_package(Python3 REQUIRED COMPONENTS Interpreter)
add_custom_target(gba_rom
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tools/build_gba.py" --data "${DECEPTUS_DATA_DIR}"
    WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
    COMMENT "building the GBA ROM from the original content")

add_custom_target(snes_rom
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tools/build_snes.py" --data "${DECEPTUS_DATA_DIR}"
    WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
    COMMENT "building the SNES opening; --scene prototype remains available through tools/build_snes.py")

add_custom_target(smd_poc
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tools/build_smd.py" --data "${DECEPTUS_DATA_DIR}"
    WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
    COMMENT "building the one-room Mega Drive proof of concept")

add_custom_target(amiga_poc
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tools/build_amiga.py" --data "${DECEPTUS_DATA_DIR}"
    WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
    COMMENT "building the experimental one-room Amiga 500 executable")

add_custom_target(console_roms
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tools/build_consoles.py" --data "${DECEPTUS_DATA_DIR}"
    WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
    COMMENT "building the GBA and SNES openings in parallel")

add_custom_target(convert_assets
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tools/assets.py" --import-data "${DECEPTUS_DATA_DIR}"
    BYPRODUCTS "${CMAKE_CURRENT_SOURCE_DIR}/assets/generated/room.c"
    WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
    COMMENT "converting assets from the main engine checkout")

add_library(deceptus_engine STATIC src/engine/compression.c src/engine/player/playerwater.c src/engine/player/playerrope.c src/engine/mechanisms/grabrope.c src/engine/mechanisms/moveablebox.c src/engine/mechanisms/crusher.c src/engine/world.c src/engine/entities.c src/engine/events.c src/engine/mechanisms.c src/engine/logic.c src/engine/pacing.c src/engine/arith.c src/engine/animation.c)
# private modules remain visible to IDEs, but mechanisms.c compiles them together.
set(DECEPTUS_MECHANISM_PARTS
    src/engine/mechanisms/blockingrect.c
    src/engine/mechanisms/checkpoint.c
    src/engine/mechanisms/collapsingplatform.c
    src/engine/mechanisms/damagerect.c
    src/engine/mechanisms/door.c
    src/engine/mechanisms/destructibleblockingrect.c
    src/engine/mechanisms/gamemechanism.c
    src/engine/mechanisms/lever.c
    src/engine/mechanisms/onoffblock.c
    src/engine/mechanisms/sensorrect.c
    src/engine/mechanisms/spikeblock.c
    src/engine/mechanisms/switchanimation.c
    src/engine/mechanisms/wind.c
    src/engine/mechanisms/mechanisminternal.h)
set_source_files_properties(${DECEPTUS_MECHANISM_PARTS} PROPERTIES HEADER_FILE_ONLY TRUE)
target_sources(deceptus_engine PRIVATE ${DECEPTUS_MECHANISM_PARTS})
target_include_directories(deceptus_engine PUBLIC include)
set_target_properties(deceptus_engine PROPERTIES POSITION_INDEPENDENT_CODE ON)
add_library(deceptus_game STATIC src/game/deceptus.c src/game/campaign.c src/game/adventure.c src/game/mechanismparts.c
    src/game/roomcontent.c src/game/appearance.c src/game/enemies.c src/game/weapons/playersword.c src/game/player/playeranimation.c src/game/items/itemheadtorch.c)
set(DECEPTUS_ENEMY_PARTS
    src/game/enemies/blob_2.c
    src/game/enemies/cannon_2.c
    src/game/enemies/frog_blue.c
    src/game/enemies/orb.c)
set_source_files_properties(${DECEPTUS_ENEMY_PARTS} PROPERTIES HEADER_FILE_ONLY TRUE)
target_sources(deceptus_game PRIVATE ${DECEPTUS_ENEMY_PARTS})

target_link_libraries(deceptus_game PUBLIC deceptus_engine)
set_target_properties(deceptus_game PROPERTIES POSITION_INDEPENDENT_CODE ON)
if(MSVC)
    target_compile_options(deceptus_engine PRIVATE /W4 /WX)
    target_compile_options(deceptus_game PRIVATE /W4 /WX)
else()
    target_compile_options(deceptus_engine PRIVATE -Wall -Wextra -Werror -pedantic)
    target_compile_options(deceptus_game PRIVATE -Wall -Wextra -Werror -pedantic)
endif()

add_library(deceptus_preview SHARED src/platform/host/bridge.c assets/generated/room.c)
target_link_libraries(deceptus_preview PRIVATE deceptus_game)
add_dependencies(deceptus_preview convert_assets)

include(CTest)
if(BUILD_TESTING)
    add_executable(gba_runtime_tests tests/gba_runtime_tests.c src/platform/gba/runtime.c)
    target_include_directories(gba_runtime_tests PRIVATE include)
    target_compile_definitions(gba_runtime_tests PRIVATE memset=deceptusTestMemset memcpy=deceptusTestMemcpy)
    add_test(NAME gba_runtime COMMAND gba_runtime_tests)
    add_executable(playerrope_tests tests/playerrope_tests.c)
    target_link_libraries(playerrope_tests PRIVATE deceptus_game)
    add_test(NAME playerrope COMMAND playerrope_tests)
    add_executable(playersword_tests tests/playersword_tests.c)
    target_link_libraries(playersword_tests PRIVATE deceptus_game)
    add_test(NAME playersword COMMAND playersword_tests)
    add_executable(playerwater_tests tests/playerwater_tests.c)
    target_link_libraries(playerwater_tests PRIVATE deceptus_game)
    add_test(NAME playerwater COMMAND playerwater_tests)
    add_executable(adventure_tests tests/adventure_tests.c)
    target_link_libraries(adventure_tests PRIVATE deceptus_game)
    add_test(NAME adventure COMMAND adventure_tests)
    add_executable(animation_tests tests/animation_tests.c)
    target_link_libraries(animation_tests PRIVATE deceptus_engine)
    add_test(NAME animation COMMAND animation_tests)
    add_custom_target(inspect_content
        COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tools/content.py" --data "${DECEPTUS_DATA_DIR}"
        BYPRODUCTS "${CMAKE_CURRENT_SOURCE_DIR}/build/content/source_logic.c"
        WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}")
    add_executable(logic_source_tests tests/logic_source_tests.c build/content/source_logic.c)
    target_link_libraries(logic_source_tests PRIVATE deceptus_engine)
    add_dependencies(logic_source_tests inspect_content)
    set(LUA_REFERENCE_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/.tools/lua-src" CACHE PATH "optional original Lua source for differential tests")
    if(EXISTS "${LUA_REFERENCE_SOURCE_DIR}/lapi.c")
        file(GLOB LUA_REFERENCE_SOURCES "${LUA_REFERENCE_SOURCE_DIR}/*.c")
        list(FILTER LUA_REFERENCE_SOURCES EXCLUDE REGEX "/(lua|luac|ltests|onelua)\\.c$")
        add_library(lua_reference STATIC ${LUA_REFERENCE_SOURCES})
        target_include_directories(lua_reference PUBLIC "${LUA_REFERENCE_SOURCE_DIR}")
        if(MSVC)
            target_compile_definitions(lua_reference PRIVATE _CRT_SECURE_NO_WARNINGS)
        else()
            target_link_libraries(lua_reference PUBLIC m ${CMAKE_DL_LIBS})
        endif()
    else()
        find_package(Lua 5.4 QUIET)
        if(LUA_FOUND)
            add_library(lua_reference INTERFACE)
            target_include_directories(lua_reference INTERFACE ${LUA_INCLUDE_DIR})
            target_link_libraries(lua_reference INTERFACE ${LUA_LIBRARIES})
        endif()
    endif()
    if(TARGET lua_reference)
        get_filename_component(DECEPTUS_SOURCE_ROOT "${DECEPTUS_DATA_DIR}/.." ABSOLUTE)
        target_compile_definitions(logic_source_tests PRIVATE DECEPTUS_HAVE_LUA_REFERENCE DECEPTUS_SOURCE_ROOT="${DECEPTUS_SOURCE_ROOT}")
        target_link_libraries(logic_source_tests PRIVATE lua_reference)
    else()
        message(STATUS "Lua reference unavailable: source callback checks run without differential Lua execution")
    endif()
    add_test(NAME source_lua_logic COMMAND logic_source_tests)
    add_executable(mechanism_tests tests/mechanism_tests.c)
    target_link_libraries(mechanism_tests PRIVATE deceptus_game)
    add_test(NAME mechanisms COMMAND mechanism_tests)
    add_executable(timed_mechanism_tests tests/timed_mechanism_tests.c)
    target_link_libraries(timed_mechanism_tests PRIVATE deceptus_game)
    add_test(NAME timed_mechanisms COMMAND timed_mechanism_tests)
    add_executable(enemy_tests tests/enemy_tests.c)
    target_link_libraries(enemy_tests PRIVATE deceptus_game)
    if(TARGET lua_reference)
        target_compile_definitions(enemy_tests PRIVATE DECEPTUS_HAVE_LUA_REFERENCE DECEPTUS_SOURCE_ROOT="${DECEPTUS_SOURCE_ROOT}")
        target_link_libraries(enemy_tests PRIVATE lua_reference)
    endif()
    add_test(NAME enemies COMMAND enemy_tests)
    add_executable(campaign_tests tests/campaign_tests.c)
    target_link_libraries(campaign_tests PRIVATE deceptus_game)
    add_test(NAME campaign COMMAND campaign_tests)
    add_executable(engine_tests tests/engine_tests.c)
    target_link_libraries(engine_tests PRIVATE deceptus_game)
    add_test(NAME engine COMMAND engine_tests)
    add_test(NAME converters COMMAND "${Python3_EXECUTABLE}" -m unittest discover -s tests -p test_*.py)
    set_tests_properties(converters PROPERTIES WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}")
endif()
