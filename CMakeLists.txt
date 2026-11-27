cmake_minimum_required(VERSION 3.24)
project(snes2DHD C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})

set(ARES_DIR ${CMAKE_SOURCE_DIR}/external/ares)

# ares generates this header with sourcery. The headless runner only needs the
# optional controller crosshairs, so empty images stand in for the PNGs.
file(MAKE_DIRECTORY ${CMAKE_BINARY_DIR}/generated/ares/resource)
file(WRITE ${CMAKE_BINARY_DIR}/generated/ares/resource/resource.hpp
"#pragma once\n"
"namespace Resource::Sprite::SuperFamicom {\n"
"  inline nall::image CrosshairBlue;\n"
"  inline nall::image CrosshairGreen;\n"
"  inline nall::image CrosshairRed;\n"
"}\n"
)

add_library(snes2dhd_core STATIC
  ${ARES_DIR}/ares/ares/memory/fixed-allocator.cpp
  ${ARES_DIR}/ares/sfc/sfc.cpp
  ${ARES_DIR}/ares/component/processor/arm7tdmi/arm7tdmi.cpp
  ${ARES_DIR}/ares/component/processor/gsu/gsu.cpp
  ${ARES_DIR}/ares/component/processor/hg51b/hg51b.cpp
  ${ARES_DIR}/ares/component/processor/spc700/spc700.cpp
  ${ARES_DIR}/ares/component/processor/upd96050/upd96050.cpp
  ${ARES_DIR}/ares/component/processor/wdc65816/wdc65816.cpp
  ${ARES_DIR}/nall/nall/nall.cpp
  ${ARES_DIR}/nall/nall/sljitAllocator.cpp
  ${ARES_DIR}/libco/libco.c
  ${ARES_DIR}/thirdparty/sljit/sljit_src/sljitLir.c
  src/core/core.cpp
)

target_include_directories(snes2dhd_core PUBLIC ${CMAKE_SOURCE_DIR}/src)
target_include_directories(snes2dhd_core SYSTEM PUBLIC
  ${CMAKE_BINARY_DIR}/generated
  ${ARES_DIR}
  ${ARES_DIR}/ares
  ${ARES_DIR}/nall
  ${ARES_DIR}/thirdparty
)

target_compile_definitions(snes2dhd_core PUBLIC
  CORE_SFC
  PROFILE_PERFORMANCE
  SLJIT_HAVE_CONFIG_PRE=1
  SLJIT_HAVE_CONFIG_POST=1
  $<$<CONFIG:Debug>:BUILD_DEBUG>
  $<$<NOT:$<CONFIG:Debug>>:BUILD_RELEASE>
)

find_package(Threads REQUIRED)
target_link_libraries(snes2dhd_core PUBLIC Threads::Threads ${CMAKE_DL_LIBS})

add_executable(snes2dhd-headless src/headless/main.cpp)
target_link_libraries(snes2dhd-headless PRIVATE snes2dhd_core)

add_executable(test_reload tests/headless/test_reload.cpp)
target_link_libraries(test_reload PRIVATE snes2dhd_core)

add_executable(test_input tests/headless/test_input.cpp)
target_link_libraries(test_input PRIVATE snes2dhd_core)

add_executable(test_cartridge tests/headless/test_cartridge.cpp)
target_link_libraries(test_cartridge PRIVATE snes2dhd_core)

find_package(SDL3 REQUIRED CONFIG COMPONENTS SDL3)
add_executable(snes2dhd src/frontend/main.cpp)
target_link_libraries(snes2dhd PRIVATE snes2dhd_core SDL3::SDL3)

add_executable(test_keyboard_input tests/frontend/test_keyboard_input.cpp)
target_include_directories(test_keyboard_input PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(test_keyboard_input PRIVATE SDL3::SDL3)

add_executable(test_gamepad_input tests/frontend/test_gamepad_input.cpp)
target_include_directories(test_gamepad_input PRIVATE ${CMAKE_SOURCE_DIR}/src)

add_executable(test_frame_pacer tests/frontend/test_frame_pacer.cpp)
target_include_directories(test_frame_pacer PRIVATE ${CMAKE_SOURCE_DIR}/src)

enable_testing()
find_package(Python3 COMPONENTS Interpreter REQUIRED)
add_test(NAME headless_min_rom
  COMMAND Python3::Interpreter ${CMAKE_SOURCE_DIR}/tests/headless/test_headless.py $<TARGET_FILE:snes2dhd-headless>
)
add_test(NAME headless_reload
  COMMAND Python3::Interpreter ${CMAKE_SOURCE_DIR}/tests/headless/test_reload.py $<TARGET_FILE:test_reload>
)
add_test(NAME headless_input
  COMMAND Python3::Interpreter ${CMAKE_SOURCE_DIR}/tests/headless/test_input.py $<TARGET_FILE:test_input>
)
add_test(NAME cartridge_mapping
  COMMAND $<TARGET_FILE:test_cartridge>
)
add_test(NAME keyboard_input
  COMMAND $<TARGET_FILE:test_keyboard_input>
)
add_test(NAME gamepad_input
  COMMAND $<TARGET_FILE:test_gamepad_input>
)
add_test(NAME frame_pacer
  COMMAND $<TARGET_FILE:test_frame_pacer>
)
add_test(NAME make_min_rom
  COMMAND Python3::Interpreter ${CMAKE_SOURCE_DIR}/tests/headless/make_min_rom.py ${CMAKE_BINARY_DIR}/min.sfc
)
set_tests_properties(make_min_rom PROPERTIES FIXTURES_SETUP min_rom)
add_test(NAME frontend_smoke
  COMMAND ${CMAKE_COMMAND} -E env SDL_VIDEO_DRIVER=offscreen $<TARGET_FILE:snes2dhd> --quit-after 120 ${CMAKE_BINARY_DIR}/min.sfc
)
set_tests_properties(frontend_smoke PROPERTIES FIXTURES_REQUIRED min_rom TIMEOUT 120)
add_test(NAME frame_hashes COMMAND Python3::Interpreter ${CMAKE_SOURCE_DIR}/tests/roms/check_frame_hashes.py --headless $<TARGET_FILE:snes2dhd-headless> --roms ${CMAKE_BINARY_DIR}/test-roms --golden ${CMAKE_SOURCE_DIR}/tests/golden/frame-hashes.txt)
