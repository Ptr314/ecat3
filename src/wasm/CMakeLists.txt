cmake_minimum_required(VERSION 3.21)

# The version comes from the same VERSION file the desktop build reads, so the
# two never drift apart -- it ends up in globals.h through configure_file().
file(STRINGS ${CMAKE_CURRENT_SOURCE_DIR}/../../VERSION ECAT_VERSION LIMIT_COUNT 1)
string(STRIP "${ECAT_VERSION}" ECAT_VERSION)
if(NOT ECAT_VERSION MATCHES "^[0-9]+\\.[0-9]+\\.[0-9]+$")
    message(FATAL_ERROR "Cannot read the project version from ../../VERSION: '${ECAT_VERSION}'")
endif()
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/../../VERSION)

project(eCat3_wasm VERSION ${ECAT_VERSION} LANGUAGES C CXX)


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
    -sEXPORTED_FUNCTIONS=['_main','_wasm_key_event','_wasm_mouse_event','_wasm_mouse_state','_wasm_key_event_id','_wasm_key_click_mode','_wasm_key_ids','_wasm_keyboard_picture','_wasm_keys_pressed','_wasm_leds_dark','_wasm_led_keys','_wasm_kbd_reset_count','_wasm_load_machine','_wasm_machine_base','_wasm_machine_selfcontained','_wasm_last_error','_wasm_load_file','_wasm_system_files','_wasm_open_file','_wasm_fdd_info','_wasm_fdd_eject','_wasm_fdd_protect','_wasm_fdd_save','_wasm_hdd_info','_wasm_hdd_load','_wasm_hdd_eject','_wasm_hdd_protect','_wasm_hdd_save','_wasm_host_file_create','_wasm_host_file_box','_wasm_host_file_release','_wasm_device_options','_wasm_set_device_option','_wasm_tape_info','_wasm_tape_load','_wasm_tape_control','_wasm_tape_save','_wasm_md2html','_wasm_reset','_wasm_set_volume','_wasm_set_muted','_wasm_get_screen_width','_wasm_get_screen_height','_malloc','_free'] \
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

# dsk_tools as subdirectory; only dsk_tools_core is linked, the rest is not built
set(ENABLE_DSK_TOOLS OFF CACHE BOOL "Disable command line tools" FORCE)
add_subdirectory(${LIBS_DIR}/dsk_tools/ ${CMAKE_CURRENT_BINARY_DIR}/dsk_tools EXCLUDE_FROM_ALL)

# Generate globals.h from template
set(EXTERNAL_Z80 0)
set(CPU_STOPPED 0)
configure_file(${CMAKE_CURRENT_SOURCE_DIR}/../globals.h.in ${CMAKE_CURRENT_BINARY_DIR}/globals.h @ONLY NEWLINE_STYLE LF)

# Emulator core sources: the same list as the desktop build, from one file
include(${CMAKE_CURRENT_SOURCE_DIR}/../emulator_sources.cmake)

# What only the browser build has: the Markdown renderer of the manual page
set(LIB_SOURCES
    ${LIBS_DIR}/md4c/md4c.c
    ${LIBS_DIR}/md4c/md4c-html.c
    ${LIBS_DIR}/md4c/entity.c
)

# WASM-specific sources
set(WASM_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/wasm_main.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/renderer_wasm.cpp
)

add_executable(${PROJECT_NAME}
    ${ECAT_CORE_SOURCES}
    ${LIB_SOURCES}
    ${WASM_SOURCES}
)

# Binary dir first: the generated globals.h lives there
target_include_directories(${PROJECT_NAME} PUBLIC
    ${CMAKE_CURRENT_BINARY_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/..
    ${LIBS_DIR}/dsk_tools/include/
)

target_link_libraries(${PROJECT_NAME} PRIVATE dsk_tools_core)

# Output as ecat3.js + ecat3.wasm
set_target_properties(${PROJECT_NAME} PROPERTIES
    OUTPUT_NAME "ecat3"
    SUFFIX ".js"
)
