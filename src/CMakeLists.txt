cmake_minimum_required(VERSION 3.16)
project(eCat3 VERSION 1.0 LANGUAGES C CXX)

set(CMAKE_INCLUDE_CURRENT_DIR ON)
set(CMAKE_AUTOUIC ON)

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

qt_add_executable(eCat3 WIN32 MACOSX_BUNDLE
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
    tests/i8080.asm
    dialogs/openconfigwindow.h dialogs/openconfigwindow.cpp dialogs/openconfigwindow.ui
    emulator/devices/common/wd1793.h emulator/devices/common/wd1793.cpp
    emulator/devices/common/fdd.h emulator/devices/common/fdd.cpp
    tests/8080exe.asm tests/8080pre.asm

    libs/crc16.h
    emulator/devices/cpu/cpulogger.h
    emulator/devices/common/i8257.h
    emulator/devices/common/i8275.h
    emulator/devices/common/i8275display.h
    emulator/devices/common/i8253.h
    emulator/files.h
    emulator/devices/common/register.h
    tests/i8253_test.asm

)
target_link_libraries(eCat3 PRIVATE
    Qt::Core
    Qt::Gui
    SDL2::SDL2main
    SDL2::SDL2
    i8080
)

if((QT_VERSION_MAJOR GREATER 4))
    target_link_libraries(eCat3 PRIVATE
        Qt::Widgets
    )
endif()

install(TARGETS eCat3
    BUNDLE DESTINATION .
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

qt_generate_deploy_app_script(
    TARGET eCat3
    FILENAME_VARIABLE deploy_script
    NO_UNSUPPORTED_PLATFORM_ERROR
)
install(SCRIPT ${deploy_script})

find_package(Qt6 REQUIRED COMPONENTS LinguistTools)

qt_add_translations(eCat3 TS_FILES
    translations/en_US.ts
    translations/ru_RU.ts
)
