# The emulator core: Qt free, shared by every build - the windowed and the
# console targets of src/CMakeLists.txt and the browser build of
# src/wasm/CMakeLists.txt. A core file is added here and nowhere else; the two
# hand-kept copies this replaced let a file reach the desktop and break the web
# build only.
#
# Defines ECAT_CORE_SOURCES, absolute paths. Reads EXTERNAL_Z80 (unset = the
# built-in Z80 core). A source that needs a library of one build only (Qt, SDL)
# compiles to nothing without it, like audio_driver_sdl.cpp without
# USE_SDL_AUDIO.

set(ECAT_CORE_SOURCES
    emulator/config.cpp emulator/config.h
    emulator/config_ext.cpp emulator/config_ext.h
    emulator/cache.cpp emulator/cache.h
    emulator/config_fields.cpp emulator/config_fields.h
    emulator/state.cpp emulator/state.h
    emulator/state_save.cpp emulator/state_save.h
    emulator/base64.cpp emulator/base64.h
    emulator/core.cpp emulator/core.h
    emulator/debug.h
    emulator/result.h
    emulator/emulator.cpp emulator/emulator.h
    emulator/thread_compat.h
    emulator/utils.cpp emulator/utils.h
    emulator/files.h emulator/files.cpp
    emulator/renderer.h
    emulator/disasm.h emulator/disasm.cpp
    emulator/disasm_pdp11.h emulator/disasm_pdp11.cpp
    emulator/disasm_x86.h emulator/disasm_x86.cpp

    emulator/script/script_types.h
    emulator/script/script_parser.cpp emulator/script/script_parser.h
    emulator/script/script_log.cpp emulator/script/script_log.h
    emulator/script/script_engine.cpp emulator/script/script_engine.h
    emulator/script/script_recorder.cpp emulator/script/script_recorder.h

    emulator/devices/cpu/i8080.cpp emulator/devices/cpu/i8080.h
    emulator/devices/cpu/i8080_context.h
    emulator/devices/cpu/i8080core.h emulator/devices/cpu/i8080core.cpp
    emulator/devices/cpu/6502core.h emulator/devices/cpu/6502core.cpp
    emulator/devices/cpu/6502.h emulator/devices/cpu/6502.cpp
    emulator/devices/cpu/pdp11_context.h emulator/devices/cpu/pdp11core.h emulator/devices/cpu/pdp11core.cpp
    emulator/devices/cpu/k1801vm1.h emulator/devices/cpu/k1801vm1.cpp
    emulator/devices/cpu/i8086_context.h emulator/devices/cpu/i8086core.h emulator/devices/cpu/i8086core.cpp
    emulator/devices/cpu/i8086.h emulator/devices/cpu/i8086.cpp
    emulator/devices/cpu/vm1_timing.h emulator/devices/cpu/vm1_timing.cpp emulator/devices/cpu/vm1_timing_table.inc
    emulator/devices/cpu/z80.h emulator/devices/cpu/z80.cpp
    emulator/devices/cpu/cpu_utils.h

    emulator/devices/common/i8255.cpp emulator/devices/common/i8255.h
    emulator/devices/common/keyboard.cpp emulator/devices/common/keyboard.h
    emulator/devices/common/scankeyboard.cpp emulator/devices/common/scankeyboard.h
    emulator/devices/common/mapkeyboard.h emulator/devices/common/mapkeyboard.cpp
    emulator/devices/common/speaker.cpp emulator/devices/common/speaker.h
    emulator/devices/common/ay8910.cpp emulator/devices/common/ay8910.h
    emulator/devices/common/covox.cpp emulator/devices/common/covox.h
    emulator/devices/common/sound.h emulator/devices/common/sound.cpp
    emulator/devices/common/tape.cpp emulator/devices/common/tape.h
    emulator/devices/common/wd1793.h emulator/devices/common/wd1793.cpp
    emulator/devices/common/gmd70.h emulator/devices/common/gmd70.cpp
    emulator/devices/common/fdd.h emulator/devices/common/fdd.cpp
    emulator/devices/common/joystick.h emulator/devices/common/joystick.cpp
    emulator/devices/common/connector.h emulator/devices/common/connector.cpp
    emulator/devices/common/mouse.h emulator/devices/common/mouse.cpp
    emulator/devices/common/i8257.h emulator/devices/common/i8257.cpp
    emulator/devices/common/i8275.h emulator/devices/common/i8275.cpp
    emulator/devices/common/i8275display.h emulator/devices/common/i8275display.cpp
    emulator/devices/common/i8251.h emulator/devices/common/i8251.cpp
    emulator/devices/common/dl11.h emulator/devices/common/dl11.cpp
    emulator/devices/common/virq_line.h
    emulator/host_serial.h emulator/host_serial.cpp
    emulator/devices/common/i8253.h emulator/devices/common/i8253.cpp
    emulator/devices/common/i8259.h emulator/devices/common/i8259.cpp
    emulator/devices/common/register.h emulator/devices/common/register.cpp
    emulator/devices/common/page_mapper.h emulator/devices/common/page_mapper.cpp
    emulator/devices/common/plane_pair.h emulator/devices/common/plane_pair.cpp
    emulator/devices/common/indirect_memory.h emulator/devices/common/indirect_memory.cpp
    emulator/devices/common/generator.h emulator/devices/common/generator.cpp
    emulator/devices/common/raster_display.h emulator/devices/common/raster_display.cpp
    emulator/devices/common/ram_address.h emulator/devices/common/ram_address.cpp
    emulator/devices/common/mux.h emulator/devices/common/mux.cpp

    emulator/devices/specific/o128display.cpp emulator/devices/specific/o128display.h
    emulator/devices/specific/agat_fdc140.h emulator/devices/specific/agat_fdc140.cpp
    emulator/devices/specific/agat_fdc840.h emulator/devices/specific/agat_fdc840.cpp
    emulator/devices/specific/agat_7_display.h emulator/devices/specific/agat_7_display.cpp
    emulator/devices/specific/agat_9_display.h emulator/devices/specific/agat_9_display.cpp
    emulator/devices/specific/agat_9_mapper.h emulator/devices/specific/agat_9_mapper.cpp
    emulator/devices/specific/agat_yazs.h emulator/devices/specific/agat_yazs.cpp
    emulator/devices/specific/uknc_channels.h emulator/devices/specific/uknc_channels.cpp
    emulator/devices/specific/uknc_display.h emulator/devices/specific/uknc_display.cpp
    emulator/devices/specific/uknc_graphics.h emulator/devices/specific/uknc_graphics.cpp
    emulator/devices/specific/uknc_hdd.h emulator/devices/specific/uknc_hdd.cpp
    emulator/devices/specific/smk512.h emulator/devices/specific/smk512.cpp
    emulator/devices/specific/uknc_timer.h emulator/devices/specific/uknc_timer.cpp
    emulator/devices/specific/uknc_keyboard.h emulator/devices/specific/uknc_keyboard.cpp
    emulator/devices/specific/uknc_sound.h emulator/devices/specific/uknc_sound.cpp
    emulator/devices/specific/argo_keyboard.h emulator/devices/specific/argo_keyboard.cpp
    emulator/devices/specific/argo_memory.h emulator/devices/specific/argo_memory.cpp
    emulator/devices/specific/zx_keyboard.h emulator/devices/specific/zx_keyboard.cpp
    emulator/devices/specific/unior_memory.h emulator/devices/specific/unior_memory.cpp
    emulator/devices/specific/agat_common.h
    emulator/devices/specific/irisha_display.h emulator/devices/specific/irisha_display.cpp
    emulator/devices/specific/bk_display.h emulator/devices/specific/bk_display.cpp
    emulator/devices/specific/poisk.h emulator/devices/specific/poisk.cpp
    emulator/devices/specific/bk_timer.h emulator/devices/specific/bk_timer.cpp
    emulator/devices/specific/bk_fdc.h emulator/devices/specific/bk_fdc.cpp

    emulator/audio/audio_driver.h
    emulator/audio/audio_driver_miniaudio.h emulator/audio/audio_driver_miniaudio.cpp
    emulator/audio/audio_driver_sdl.h emulator/audio/audio_driver_sdl.cpp

    libs/crc16.h libs/crc16.cpp
    libs/lodepng/lodepng.cpp libs/lodepng/lodepng.h
    libs/mfm_formats.h
    libs/mfm_tools.h libs/mfm_tools.cpp
    libs/zip_reader.h libs/zip_reader.cpp
    libs/zip_writer.h libs/zip_writer.cpp
    libs/audio_filters.h
    libs/miniaudio/miniaudio.c

    libs/ini_wrapper.h libs/ini_wrapper.cpp
)

if(EXTERNAL_Z80)
    list(APPEND ECAT_CORE_SOURCES libs/z80.hpp)
else()
    list(APPEND ECAT_CORE_SOURCES
        emulator/devices/cpu/z80_context.h
        emulator/devices/cpu/z80core.h emulator/devices/cpu/z80core.cpp
    )
endif()

list(TRANSFORM ECAT_CORE_SOURCES PREPEND "${CMAKE_CURRENT_LIST_DIR}/")
