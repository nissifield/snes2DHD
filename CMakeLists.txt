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

enable_testing()
find_package(Python3 COMPONENTS Interpreter REQUIRED)
add_test(NAME headless_min_rom
  COMMAND Python3::Interpreter ${CMAKE_SOURCE_DIR}/tests/headless/test_headless.py $<TARGET_FILE:snes2dhd-headless>
)
add_test(NAME headless_reload
  COMMAND Python3::Interpreter ${CMAKE_SOURCE_DIR}/tests/headless/test_reload.py $<TARGET_FILE:test_reload>
)
