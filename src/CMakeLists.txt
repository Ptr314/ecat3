cmake_minimum_required(VERSION 3.16)

project(eCat3 VERSION 3.0.0 LANGUAGES C CXX)

set(EXTERNAL_Z80 0)

set(LOGGER 0)

if (${LOGGER})
    if (${EXTERNAL_Z80})
        set(LOG_NAME "Z80_EXT")
    else()
        set(LOG_NAME "LOG")
    endif()
    set(LOG_LIMIT 300000)
    set(LOG_8255 1)
    set(LOG_CPU 1)
endif()

set(CMAKE_INCLUDE_CURRENT_DIR ON)
set(CMAKE_AUTOUIC ON)

configure_file (globals.h.in ${CMAKE_CURRENT_SOURCE_DIR}/globals.h @ONLY)

find_package(QT NAMES Qt5 Qt6 REQUIRED COMPONENTS Core)
find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS Gui)
find_package(Qt${QT_VERSION_MAJOR} OPTIONAL_COMPONENTS Widgets)

find_package(SDL2 REQUIRED)
#include_directories(${SDL2_INCLUDE_DIRS})
add_subdirectory(${PROJECT_SOURCE_DIR}/libs/intel8080-emulator)

qt_standard_project_setup()

set(RESOURCE_FILES resources.qrc)
QT6_ADD_RESOURCES(PROJECT_RESOURCES ${RESOURCE_FILES})

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

qt_add_executable(${PROJECT_NAME} WIN32 MACOSX_BUNDLE
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
    ${PROJECT_RESOURCES}
    emulator/devices/cpu/i8080_context.h
    emulator/devices/cpu/i8080core.h emulator/devices/cpu/i8080core.cpp
#    emulator/devices/cpu/i8080core2.h emulator/devices/cpu/i8080core2.cpp
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
    emulator/devices/cpu/cpulogger.h
    emulator/devices/common/i8257.h
    emulator/devices/common/i8275.h
    emulator/devices/common/i8275display.h
    emulator/devices/common/i8253.h
    emulator/files.h emulator/files.cpp
    emulator/devices/common/register.h
    libs/lodepng/lodepng.cpp libs/lodepng/lodepng.h
    emulator/devices/cpu/z80.h
    emulator/devices/cpu/z80.cpp
    emulator/devices/cpu/cpu_utils.h
    emulator/devices/common/page_mapper.h
    dialogs/hexeditorline.h dialogs/hexeditorline.cpp
    dialogs/dosframe.h dialogs/dosframe.cpp
    emulator/devices/common/generator.h

    tests/8080exe.asm tests/8080pre.asm
    tests/zexall.asm
    tests/i8253_test.asm
    tests/i8080.asm

    emulator/logger.h

    dialogs/aboutdlg.ui

    ${Z80_FILES}
)

target_link_libraries(${PROJECT_NAME} PRIVATE
    Qt::Core
    Qt::Gui
    SDL2::SDL2main
    SDL2::SDL2
    i8080
)

if((QT_VERSION_MAJOR GREATER 4))
    target_link_libraries(${PROJECT_NAME} PRIVATE
        Qt::Widgets
    )
endif()

install(TARGETS ${PROJECT_NAME}
    BUNDLE DESTINATION .
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

qt_generate_deploy_app_script(
    TARGET ${PROJECT_NAME}
    OUTPUT_SCRIPT deploy_script
    NO_UNSUPPORTED_PLATFORM_ERROR
)
install(SCRIPT ${deploy_script})

find_package(Qt6 REQUIRED COMPONENTS LinguistTools)

qt_add_translations(${PROJECT_NAME} TS_FILES
    translations/en_US.ts
    translations/ru_RU.ts
)
