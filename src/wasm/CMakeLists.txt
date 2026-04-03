cmake_minimum_required(VERSION 3.21)

project(eCat3_wasm VERSION 3.6.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# WASM-specific defines
add_compile_definitions(WASM_BUILD)
add_compile_definitions(FORCE_STD_THREADING)
add_compile_definitions(USE_MINI_INI)

# Emscripten compiler/linker flags
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -pthread")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -pthread -fexceptions")

set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} \
    -pthread \
    -sPTHREAD_POOL_SIZE=3 \
    -sALLOW_MEMORY_GROWTH=1 \
    -sFORCE_FILESYSTEM=1 \
    -sEXPORTED_RUNTIME_METHODS=['FS','ccall','cwrap'] \
    -sEXPORTED_FUNCTIONS=['_main','_wasm_key_event','_wasm_load_machine','_wasm_load_file','_wasm_reset','_wasm_set_volume','_wasm_set_muted','_wasm_get_screen_width','_wasm_get_screen_height','_malloc','_free'] \
    -sMODULARIZE=1 \
    -sEXPORT_NAME=EmuModule \
    -sENVIRONMENT=web,worker \
    -sEXIT_RUNTIME=0 \
    -fexceptions \
    -sNO_DISABLE_EXCEPTION_CATCHING \
")

# Source directories
set(EMU_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../emulator)
set(LIBS_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../libs)

# dsk_tools as subdirectory
set(ENABLE_DSK_TOOLS OFF CACHE BOOL "Disable command line tools" FORCE)
add_subdirectory(${LIBS_DIR}/dsk_tools/ ${CMAKE_CURRENT_BINARY_DIR}/dsk_tools)

# Generate globals.h from template
set(EXTERNAL_Z80 0)
set(CPU_STOPPED 0)
set(LOGGER 0)
configure_file(${CMAKE_CURRENT_SOURCE_DIR}/../globals.h.in ${CMAKE_CURRENT_BINARY_DIR}/globals.h @ONLY NEWLINE_STYLE LF)

# Emulator core sources
set(EMULATOR_SOURCES
    ${EMU_DIR}/config.cpp
    ${EMU_DIR}/core.cpp
    ${EMU_DIR}/emulator.cpp
    ${EMU_DIR}/utils.cpp
    ${EMU_DIR}/disasm.cpp
    ${EMU_DIR}/files.cpp
    ${EMU_DIR}/devices/cpu/i8080.cpp
    ${EMU_DIR}/devices/cpu/i8080core.cpp
    ${EMU_DIR}/devices/cpu/z80.cpp
    ${EMU_DIR}/devices/cpu/z80core.cpp
    ${EMU_DIR}/devices/cpu/6502.cpp
    ${EMU_DIR}/devices/cpu/6502core.cpp
    ${EMU_DIR}/devices/common/i8255.cpp
    ${EMU_DIR}/devices/common/i8251.cpp
    ${EMU_DIR}/devices/common/i8253.cpp
    ${EMU_DIR}/devices/common/i8259.cpp
    ${EMU_DIR}/devices/common/keyboard.cpp
    ${EMU_DIR}/devices/common/scankeyboard.cpp
    ${EMU_DIR}/devices/common/mapkeyboard.cpp
    ${EMU_DIR}/devices/common/speaker.cpp
    ${EMU_DIR}/devices/common/sound.cpp
    ${EMU_DIR}/devices/common/tape.cpp
    ${EMU_DIR}/devices/common/fdd.cpp
    ${EMU_DIR}/devices/common/wd1793.cpp
    ${EMU_DIR}/devices/common/page_mapper.cpp
    ${EMU_DIR}/devices/common/generator.cpp
    ${EMU_DIR}/devices/common/register.cpp
    ${EMU_DIR}/devices/common/raster_display.cpp
    ${EMU_DIR}/devices/common/ram_address.cpp
    ${EMU_DIR}/devices/common/gmd70.cpp
    ${EMU_DIR}/devices/specific/o128display.cpp
    ${EMU_DIR}/devices/specific/agat_fdc140.cpp
    ${EMU_DIR}/devices/specific/agat_fdc840.cpp
    ${EMU_DIR}/devices/specific/agat_7_display.cpp
    ${EMU_DIR}/devices/specific/agat_9_display.cpp
    ${EMU_DIR}/devices/specific/agat_9_mapper.cpp
    ${EMU_DIR}/devices/specific/irisha_display.cpp
    ${EMU_DIR}/audio/audio_driver_miniaudio.cpp
)

# Library sources
set(LIB_SOURCES
    ${LIBS_DIR}/ini_wrapper.cpp
    ${LIBS_DIR}/crc16.cpp
    ${LIBS_DIR}/mfm_tools.cpp
    ${LIBS_DIR}/lodepng/lodepng.cpp
    ${LIBS_DIR}/miniaudio/miniaudio.c
)

# WASM-specific sources
set(WASM_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/wasm_main.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/renderer_wasm.cpp
)

add_executable(${PROJECT_NAME}
    ${EMULATOR_SOURCES}
    ${LIB_SOURCES}
    ${WASM_SOURCES}
)

# Binary dir first so generated globals.h takes precedence over source tree version
target_include_directories(${PROJECT_NAME} PUBLIC
    ${CMAKE_CURRENT_BINARY_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/..
    ${LIBS_DIR}/dsk_tools/include/
)

target_link_libraries(${PROJECT_NAME} PRIVATE dsk_tools)

# Output as ecat3.js + ecat3.wasm
set_target_properties(${PROJECT_NAME} PROPERTIES
    OUTPUT_NAME "ecat3"
    SUFFIX ".js"
)
