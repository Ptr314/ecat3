cmake_minimum_required(VERSION 3.16)
project(eCat3 VERSION 1.0 LANGUAGES C CXX)

set(CMAKE_INCLUDE_CURRENT_DIR ON)
set(CMAKE_AUTOUIC ON)

find_package(QT NAMES Qt5 Qt6 REQUIRED COMPONENTS Core)
find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS Gui)
find_package(Qt${QT_VERSION_MAJOR} OPTIONAL_COMPONENTS Widgets)

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
)
target_link_libraries(eCat3 PRIVATE
    Qt::Core
    Qt::Gui
)


# Resources:
#set_source_files_properties("resources/icons/exit.png"
#    PROPERTIES QT_RESOURCE_ALIAS "exit"
#)
#set_source_files_properties("resources/icons/ksim_cpu.png"
#    PROPERTIES QT_RESOURCE_ALIAS "devices"
#)
#set_source_files_properties("resources/icons/next.png"
#    PROPERTIES QT_RESOURCE_ALIAS "next"
#)
#set_source_files_properties("resources/icons/power.png"
#    PROPERTIES QT_RESOURCE_ALIAS "power"
#)
#set_source_files_properties("resources/icons/reload.png"
#    PROPERTIES QT_RESOURCE_ALIAS "reload"
#)
#set_source_files_properties("resources/icons/tv.png"
#    PROPERTIES QT_RESOURCE_ALIAS "tv"
#)
#set(resources_resource_files
#    "resources/icons/exit.png"
#    "resources/icons/ksim_cpu.png"
#    "resources/icons/next.png"
#    "resources/icons/power.png"
#    "resources/icons/reload.png"
#    "resources/icons/tv.png"
#)

#qt_add_resources(eCat3 "resources"
#    PREFIX
#        "/icons"
#    FILES
#        ${resources_resource_files}
#)
#set_source_files_properties("resources/fonts/RobotoMono-Regular.ttf"
#    PROPERTIES QT_RESOURCE_ALIAS "mono"
#)
#set(resources1_resource_files
#    "resources/fonts/RobotoMono-Regular.ttf"
#)

#qt_add_resources(eCat3 "resources1"
#    PREFIX
#        "/fonts"
#    FILES
#        ${resources1_resource_files}
#)

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
