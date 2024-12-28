cmake_minimum_required(VERSION 3.16)

project(eCat3 VERSION 3.2.1 LANGUAGES C CXX)

set(USE_SDL 1)

if (${USE_SDL})
    if(CMAKE_SYSTEM_NAME STREQUAL "Darwin" OR CMAKE_SYSTEM_NAME STREQUAL "Linux")
        set(SDL_SEPARATE_WINDOW 1)
    else()
        set(SDL_SEPARATE_WINDOW 0)
    endif()
endif()


IF(CMAKE_BUILD_TYPE MATCHES DEBUG)
    set(EXTERNAL_Z80 0)
    set(CPU_STOPPED 0)
    set(LOGGER 0)
ENDIF(CMAKE_BUILD_TYPE MATCHES DEBUG)

if (${LOGGER})
    if (${EXTERNAL_Z80})
        set(LOG_NAME "Z80_EXT")
    else()
        set(LOG_NAME "LOG")
    endif()
    set(LOG_LIMIT 200000)
    set(LOG_8255 0)
    set(LOG_CPU 0)
    set(LOG_MAPPER 0)
    set(LOG_PORTS 0)
    set(LOG_FDD 0)
    set(LOG_PAGE_MAPPER 0)
    set(LOG_INTERFACES 0)
endif()

set(CMAKE_INCLUDE_CURRENT_DIR ON)
set(CMAKE_AUTOUIC ON)

set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

configure_file (globals.h.in ${CMAKE_CURRENT_SOURCE_DIR}/globals.h @ONLY)

find_package(QT NAMES Qt6 Qt5 REQUIRED COMPONENTS Widgets LinguistTools)
find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS Widgets LinguistTools)

find_package(SDL2 REQUIRED)

set(RESOURCE_FILES resources.qrc)

if(${QT_VERSION} VERSION_LESS 5.15.0)
    qt5_add_resources(PROJECT_RESOURCES ${RESOURCE_FILES})
else()
    qt_add_resources(PROJECT_RESOURCES ${RESOURCE_FILES})
endif()


set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

if (${EXTERNAL_Z80})
    set(Z80_FILES
        libs/z80.hpp
    )
else()
    set(Z80_FILES
        emulator/devices/cpu/z80_context.h
        emulator/devices/cpu/z80core.h emulator/devices/cpu/z80core.cpp
    )
endif()

set(PROJECT_SOURCES
    ../README.md
    ../BUILD.md

    dialogs/dumparea.cpp dialogs/dumparea.h
    dialogs/dumpwindow.cpp dialogs/dumpwindow.h dialogs/dumpwindow.ui
    dialogs/mmwindow.cpp dialogs/mmwindow.h dialogs/mmwindow.ui
    emulator/config.cpp emulator/config.h
    emulator/core.cpp emulator/core.h
    emulator/debug.h
    emulator/devices/common/i8255.cpp emulator/devices/common/i8255.h
    emulator/devices/common/keyboard.cpp emulator/devices/common/keyboard.h
    emulator/devices/common/scankeyboard.cpp emulator/devices/common/scankeyboard.h
    emulator/devices/common/speaker.cpp emulator/devices/common/speaker.h
    emulator/devices/common/tape.cpp emulator/devices/common/tape.h
    emulator/devices/cpu/i8080.cpp emulator/devices/cpu/i8080.h
    emulator/devices/specific/o128display.cpp emulator/devices/specific/o128display.h
    emulator/emulator.cpp emulator/emulator.h
    emulator/utils.cpp emulator/utils.h
    main.cpp
    mainwindow.cpp mainwindow.h mainwindow.ui
    emulator/devices/cpu/i8080_context.h
    emulator/devices/cpu/i8080core.h emulator/devices/cpu/i8080core.cpp
    dialogs/debugwindow.h dialogs/debugwindow.cpp dialogs/debugwindow.ui
    dialogs/disasmarea.h dialogs/disasmarea.cpp
    emulator/disasm.h emulator/disasm.cpp
    dialogs/dialogs.h
    dialogs/keyvaluearea.h dialogs/keyvaluearea.cpp
    dialogs/portwindow.h dialogs/portwindow.cpp dialogs/portwindow.ui
    dialogs/i8255window.h dialogs/i8255window.cpp dialogs/i8255window.ui
    dialogs/openconfigwindow.h dialogs/openconfigwindow.cpp dialogs/openconfigwindow.ui
    emulator/devices/common/wd1793.h emulator/devices/common/wd1793.cpp
    emulator/devices/common/fdd.h emulator/devices/common/fdd.cpp
    libs/crc16.h libs/crc16.cpp
    emulator/devices/common/i8257.h
    emulator/devices/common/i8275.h
    emulator/devices/common/i8275display.h
    emulator/devices/common/i8253.h
    emulator/files.h emulator/files.cpp
    emulator/devices/common/register.h emulator/devices/common/register.cpp
    libs/lodepng/lodepng.cpp libs/lodepng/lodepng.h
    emulator/devices/cpu/z80.h
    emulator/devices/cpu/z80.cpp
    emulator/devices/cpu/cpu_utils.h
    emulator/devices/common/page_mapper.h emulator/devices/common/page_mapper.cpp
    dialogs/hexeditorline.h dialogs/hexeditorline.cpp
    dialogs/dosframe.h dialogs/dosframe.cpp
    emulator/devices/common/generator.h  emulator/devices/common/generator.cpp

    tests/8080exe.asm tests/8080pre.asm
    tests/zexall.asm
    tests/i8253_test.asm
    tests/i8080.asm

    emulator/logger.h

    dialogs/aboutdlg.ui
    dialogs/genericdbgwnd.h dialogs/genericdbgwnd.cpp

    emulator/devices/cpu/6502core.h emulator/devices/cpu/6502core.cpp
    emulator/devices/cpu/6502.h emulator/devices/cpu/6502.cpp

    ${Z80_FILES}
    emulator/devices/specific/agat_fdc140.h emulator/devices/specific/agat_fdc140.cpp
    emulator/devices/specific/agat_display.h emulator/devices/specific/agat_display.cpp
    emulator/devices/common/mapkeyboard.h emulator/devices/common/mapkeyboard.cpp
    emulator/devices/common/sound.h emulator/devices/common/sound.cpp
    libs/mfm_formats.h
    libs/mfm_tools.h libs/mfm_tools.cpp

    libs/md4c/md4c.h libs/md4c/md4c.c libs/md4c/md4c-html.h libs/md4c/md4c-html.c libs/md4c/entity.h libs/md4c/entity.c

    dialogs/taperecorder.h dialogs/taperecorder.cpp dialogs/taperecorder.ui
)

set(TRANSLATION_FILES
    translations/en_US.ts
    translations/ru_RU.ts
)

if(${QT_VERSION_MAJOR} GREATER_EQUAL 6)
    find_package(Qt6 REQUIRED COMPONENTS LinguistTools)

    qt_add_executable(${PROJECT_NAME} WIN32 MACOSX_BUNDLE
        MANUAL_FINALIZATION
        ${PROJECT_SOURCES}
        ${PROJECT_RESOURCES}
    )
    qt_add_translations(${PROJECT_NAME} TS_FILES
        ${TRANSLATION_FILES}
    )
else()
    add_executable(${PROJECT_NAME} WIN32 MACOSX_BUNDLE
        ${PROJECT_SOURCES}
        ${PROJECT_RESOURCES}
    )
    qt5_add_translation(QM_FILES ${TRANSLATION_FILES})

    # find_program(LUPDATE_EXECUTABLE lupdate)
    # find_program(LRELEASE_EXECUTABLE lrelease)

    # file(MAKE_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/languages/)

    # foreach(_ts_file ${TRANSLATION_FILES})

    #     # execute_process(
    #     #     COMMAND ${LUPDATE_EXECUTABLE} -recursive ${CMAKE_SOURCE_DIR} -ts ${_ts_file})
    #     get_filename_component(_qt_id ${_ts_file} NAME_WE)
    #     execute_process(
    #         COMMAND ${LRELEASE_EXECUTABLE} "${CMAKE_SOURCE_DIR}/${_ts_file}" -qm ${CMAKE_CURRENT_BINARY_DIR}/languages/${_qt_id}.qm )

    # endforeach()
endif()

target_link_libraries(${PROJECT_NAME} PRIVATE
    Qt${QT_VERSION_MAJOR}::Widgets
    SDL2::SDL2main
    SDL2::SDL2
)

if(QT_VERSION_MAJOR EQUAL 6)
    qt_finalize_executable(${PROJECT_NAME})
endif()
