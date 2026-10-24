# replay-player: one player for every usd-replay/1 replay, on the Strata engine.
#
#   cmake -S . -B build && cmake --build build          the desktop player (bundles)
#   -DREPLAY_PLAYER_WITH_USD=ON -DCMAKE_PREFIX_PATH=<OpenUSD>   also .usda/.usdc, through
#                                                       usd-recorder's C++ reader
#   emcmake cmake -S . -B build-web -DREPLAY_PLAYER_WEB=ON      the browser player
#   -DREPLAY_PLAYER_WITH_STRATA=OFF                     the model, decoder and tests only (no GPU)
cmake_minimum_required(VERSION 3.24)
project(replay_player VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

option(REPLAY_PLAYER_WARNINGS_AS_ERRORS "Treat compiler warnings as errors" OFF)
option(REPLAY_PLAYER_BUILD_TESTS "Build the tests" ON)
option(REPLAY_PLAYER_WITH_STRATA "Build the player on the Strata engine" ON)
option(REPLAY_PLAYER_WITH_USD "Open .usda/.usdc replays with usd-recorder's C++ reader (needs OpenUSD)" OFF)
option(REPLAY_PLAYER_WEB "Build the browser player (Emscripten + Strata's WebGL 2 backend)" OFF)
option(REPLAY_PLAYER_STRATA_TOOLS "Also build Strata's tools (strata-imgcmp, for the golden images)" OFF)

# Strata v0.5.0 (ABI 1.11): nearest-texel textures (1.10) for fields, filled rectangles (1.11)
# for the HUD. Fetched at this commit, or taken from -DFETCHCONTENT_SOURCE_DIR_STRATA=<checkout>.
set(REPLAY_PLAYER_STRATA_REF "2f997acad88d22b1249ee67ed905ba50e6a661e8" CACHE STRING "Strata commit (v0.5.0)")
# usd-recorder's C++ reader (cpp/reader), for .usda/.usdc. Or -DFETCHCONTENT_SOURCE_DIR_USD_RECORDER=<checkout>.
set(REPLAY_PLAYER_USD_RECORDER_REF "36ccc065df27fa6f7b00f237c0ab4764b111912d" CACHE STRING "usd-recorder commit")

if(EMSCRIPTEN)
  # Strata compiles C++ exceptions as wasm exceptions; everything in one .wasm must match.
  add_compile_options("$<$<COMPILE_LANGUAGE:CXX>:-fwasm-exceptions>")
  add_link_options(-fwasm-exceptions -sALLOW_MEMORY_GROWTH=1)
  if(REPLAY_PLAYER_WITH_USD)
    message(FATAL_ERROR "OpenUSD is not built for WebAssembly: the browser player reads usd-web bundles")
  endif()
endif()
if(REPLAY_PLAYER_WEB AND NOT EMSCRIPTEN)
  message(FATAL_ERROR "REPLAY_PLAYER_WEB needs Emscripten (emcmake cmake ...)")
endif()

function(rp_warnings target)
  if(MSVC)
    target_compile_options(${target} PRIVATE /W4)
    if(REPLAY_PLAYER_WARNINGS_AS_ERRORS)
      target_compile_options(${target} PRIVATE /WX)
    endif()
  else()
    target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic -Wshadow)
    if(REPLAY_PLAYER_WARNINGS_AS_ERRORS)
      target_compile_options(${target} PRIVATE -Werror)
    endif()
  endif()
endfunction()

# The model and the bundle decoder: no engine, no GPU, no dependencies.
add_library(rp_model STATIC src/model/json.cpp src/model/gunzip.cpp src/model/model.cpp src/model/bundle.cpp
                            src/play/playback.cpp)
target_include_directories(rp_model PUBLIC src)
rp_warnings(rp_model)
# The decoder's dequantization must give the same doubles as usd-web's numpy (min + q * step), so
# no fused multiply-adds.
if(NOT MSVC)
  target_compile_options(rp_model PRIVATE -ffp-contract=off)
endif()

if(NOT EMSCRIPTEN)
  add_executable(replay-decode tools/replay_decode.cpp)
  target_link_libraries(replay-decode PRIVATE rp_model)
  rp_warnings(replay-decode)
endif()

include(FetchContent)
if(REPLAY_PLAYER_WITH_USD)
  FetchContent_Declare(usd_recorder
    GIT_REPOSITORY https://github.com/Preponderous-Software/usd-recorder.git
    GIT_TAG ${REPLAY_PLAYER_USD_RECORDER_REF}
    SOURCE_SUBDIR cpp)
  set(USD_RECORDER_BUILD_READER ON CACHE BOOL "" FORCE)
  FetchContent_MakeAvailable(usd_recorder)
  add_library(rp_usd STATIC src/model/usd_source.cpp)
  target_link_libraries(rp_usd PUBLIC rp_model usd_recorder::replay_reader)
  target_compile_definitions(rp_usd PUBLIC REPLAY_PLAYER_WITH_USD)
  rp_warnings(rp_usd)
  add_executable(replay-compare tools/replay_compare.cpp)
  target_link_libraries(replay-compare PRIVATE rp_usd)
  rp_warnings(replay-compare)
endif()

if(REPLAY_PLAYER_BUILD_TESTS)
  enable_testing()
  foreach(t json gunzip bundle playback)
    add_executable(test_${t} tests/test_${t}.cpp)
    target_link_libraries(test_${t} PRIVATE rp_model)
    target_include_directories(test_${t} PRIVATE tests)
    rp_warnings(test_${t})
    if(EMSCRIPTEN)
      target_link_options(test_${t} PRIVATE -sNODERAWFS=1)
    endif()
    add_test(NAME ${t} COMMAND test_${t} "${CMAKE_CURRENT_SOURCE_DIR}/tests/data")
  endforeach()
endif()

if(REPLAY_PLAYER_WITH_STRATA OR REPLAY_PLAYER_WEB)
  set(STRATA_BUILD_TESTS OFF CACHE BOOL "" FORCE)
  set(STRATA_BUILD_TOOLS ${REPLAY_PLAYER_STRATA_TOOLS} CACHE BOOL "" FORCE)
  set(STRATA_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
  set(STRATA_WITH_USD OFF CACHE BOOL "" FORCE)
  set(STRATA_WITH_SIM OFF CACHE BOOL "" FORCE)
  FetchContent_Declare(strata
    GIT_REPOSITORY https://github.com/Preponderous-Software/strata.git
    GIT_TAG ${REPLAY_PLAYER_STRATA_REF})
  FetchContent_MakeAvailable(strata)

  add_library(rp_render STATIC src/render/scene.cpp src/ui/hud.cpp)
  target_link_libraries(rp_render PUBLIC rp_model strata::static)
  rp_warnings(rp_render)
endif()

if(REPLAY_PLAYER_WITH_STRATA AND NOT EMSCRIPTEN)
  add_executable(replay-player app/main.cpp)
  target_link_libraries(replay-player PRIVATE rp_render)
  if(REPLAY_PLAYER_WITH_USD)
    target_link_libraries(replay-player PRIVATE rp_usd)
  endif()
  target_compile_definitions(replay-player PRIVATE REPLAY_PLAYER_VERSION="${PROJECT_VERSION}")
  rp_warnings(replay-player)
endif()

# emcmake cmake -S . -B build-web -DREPLAY_PLAYER_WEB=ON: replay-player.js + .wasm, loaded by
# web/index.html (see docs/WEB.md).
if(REPLAY_PLAYER_WEB)
  add_executable(replay-player-web app/web_main.cpp)
  target_link_libraries(replay-player-web PRIVATE rp_render)
  target_compile_definitions(replay-player-web PRIVATE REPLAY_PLAYER_VERSION="${PROJECT_VERSION}")
  rp_warnings(replay-player-web)
  set_target_properties(replay-player-web PROPERTIES OUTPUT_NAME replay-player SUFFIX ".js")
  target_link_options(replay-player-web PRIVATE
    -sSTACK_SIZE=1MB
    -sENVIRONMENT=web
    -sEXIT_RUNTIME=0
    "-sEXPORTED_FUNCTIONS=_main,_malloc,_free,_rp_open,_rp_add_chunk,_rp_play,_rp_pause,_rp_seek,_rp_seek_sample,_rp_step,_rp_set_speed,_rp_set_interpolate,_rp_set_shadows,_rp_orbit,_rp_pan,_rp_zoom,_rp_reset_view,_rp_resize,_rp_frame,_rp_state,_rp_last_error"
    -sEXPORTED_RUNTIME_METHODS=UTF8ToString,HEAPU8)
endif()
