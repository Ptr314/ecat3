cmake_minimum_required(VERSION 3.21)

project(eCat3 VERSION 3.4.1 LANGUAGES C CXX)

find_package(QT NAMES Qt6 Qt5 REQUIRED COMPONENTS Widgets LinguistTools)
find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS Widgets LinguistTools)

if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    if(${QT_VERSION_MAJOR} GREATER_EQUAL 6)
        # Qt6 for Win 10+, Linux & macOS 64 bits
        # add_compile_definitions(RENDERER_SDL2)
        # add_compile_definitions(RENDERER_QT)
        add_compile_definitions(RENDERER_OPENGL)
    else()
        # Qt5 for Win 7+ 32 bits
        add_compile_definitions(RENDERER_SDL2)
        # add_compile_definitions(RENDERER_QT)
    endif()
else()
    option(RENDERER_SDL2 OFF)
    if(RENDERER_SDL2)
        add_compile_definitions(RENDERER_SDL2)
    endif()
    option(RENDERER_QT OFF)
    if(RENDERER_QT)
        add_compile_definitions(RENDERER_QT)
    endif()
    option(RENDERER_OPENGL OFF)
    if(RENDERER_OPENGL)
        add_compile_definitions(RENDERER_OPENGL)
    endif()
endif()

IF(CMAKE_BUILD_TYPE STREQUAL "Debug")
    set(EXTERNAL_Z80 0)
    set(CPU_STOPPED 0)
    set(LOGGER 0)
ENDIF()

if (${LOGGER})
    if (${EXTERNAL_Z80})
        set(LOG_NAME "Z80_EXT")
    else()
        set(LOG_NAME "LOG")
    endif()
    set(LOG_LIMIT 50000)
    set(LOG_8255 0)
    set(LOG_CPU 0)
    set(LOG_MAPPER 0)
    set(LOG_PORTS 0)
    set(LOG_FDD 1)
    set(LOG_PAGE_MAPPER 0)
    set(LOG_INTERFACES 0)
endif()

# CMake globals & options

set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_INCLUDE_CURRENT_DIR ON)

if(QT_VERSION_MAJOR EQUAL 6)
    find_package(Qt6 REQUIRED COMPONENTS Widgets OpenGLWidgets)
    set(OPENGL_FILES
        renderers/renderer_opengl.h
        renderers/GLWidget.cpp renderers/GLWidget.h
    )
endif()

# For Windows, path to SDL should be added to CMAKE_PREFIX_PATH global environment variable
find_package(SDL2 REQUIRED)
include_directories(${SDL2_INCLUDE_DIRS})

set(ENABLE_DSK_TOOLS OFF CACHE BOOL "Disable command line tools from the library")
add_subdirectory(${CMAKE_SOURCE_DIR}/libs/dsk_tools/)

# Translations

set(SUPPORTED_LANGUAGES ru en)
set(TS_FILES
    ${CMAKE_SOURCE_DIR}/translations/en_us.ts
    ${CMAKE_SOURCE_DIR}/translations/ru_ru.ts
)

# Source files

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
    ../MANUAL.md
    ../CONFIG.md
    ../HISTORY.md

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
    emulator/devices/specific/agat_fdc840.h emulator/devices/specific/agat_fdc840.cpp

    libs/md4c/md4c.h libs/md4c/md4c.c libs/md4c/md4c-html.h libs/md4c/md4c-html.c libs/md4c/entity.h libs/md4c/entity.c

    dialogs/taperecorder.h dialogs/taperecorder.cpp dialogs/taperecorder.ui

    emulator/renderer.h
    renderers/renderer_sdl2.h
    renderers/renderer_qt.h
    ${OPENGL_FILES}

    libs/audio_filters.h
    emulator/devices/common/raster_display.h
    emulator/devices/common/raster_display.cpp
)

# Adding Qt base translations

find_package(Qt${QT_VERSION_MAJOR} REQUIRED Core)
if(NOT ${Qt6Core_DIR} STREQUAL "")
    set(QT_TRANSLATIONS_DIR "${Qt6Core_DIR}/../../../translations")
else()
    set(QT_TRANSLATIONS_DIR "${Qt5Core_DIR}/../../../translations")
endif()

set(QT_TRANSLATION_FILES "")
foreach(LANG ${SUPPORTED_LANGUAGES})
    set(QT_TRANSLATION_FILE "${QT_TRANSLATIONS_DIR}/qtbase_${LANG}.qm")
    if(EXISTS ${QT_TRANSLATION_FILE})
        list(APPEND QT_TRANSLATION_FILES ${QT_TRANSLATION_FILE})
    else()
        message(WARNING "Qt translation (qtbase_${LANG}.qm) not found: ${QT_TRANSLATION_FILE}")
    endif()
endforeach()

if(QT_TRANSLATION_FILES STREQUAL "")
    message(FATAL_ERROR "No any Qt translations found.")
endif()

file(WRITE ${CMAKE_BINARY_DIR}/qt_base.qrc "<RCC>\n  <qresource prefix=\"/i18n\">\n")
foreach(FILE ${QT_TRANSLATION_FILES})
    get_filename_component(FILENAME ${FILE} NAME)
    file(APPEND ${CMAKE_BINARY_DIR}/qt_base.qrc "    <file alias=\"${FILENAME}\">${FILE}</file>\n")
endforeach()
file(APPEND ${CMAKE_BINARY_DIR}/qt_base.qrc "  </qresource>\n</RCC>\n")

set(RESOURCE_FILES
    resources.qrc
    ${CMAKE_BINARY_DIR}/qt_base.qrc
)

# Build project

if(${QT_VERSION_MAJOR} GREATER_EQUAL 6)
    QT6_ADD_RESOURCES(PROJECT_RESOURCES
        ${RESOURCE_FILES}
    )
    qt_add_executable(${PROJECT_NAME}
        MANUAL_FINALIZATION
        ${PROJECT_SOURCES}
        ${PROJECT_RESOURCES}
    )

    qt_add_translations(${PROJECT_NAME} TS_FILES
        ${TS_FILES}
    )
else()
    # For Qt5: making qms manually and putting them to a separate resource file
    find_program(LRELEASE_EXECUTABLE lrelease)

    set(QRC_FILE ${CMAKE_BINARY_DIR}/translations.qrc)
    file(WRITE ${QRC_FILE} "<RCC>\n    <qresource prefix=\"/i18n\">\n")

    foreach(_ts_file ${TS_FILES})
        get_filename_component(_qt_id ${_ts_file} NAME_WE)
        set(QM_FILE ${CMAKE_BINARY_DIR}/${_qt_id}.qm)
        execute_process(COMMAND ${LRELEASE_EXECUTABLE} ${_ts_file} -qm  ${QM_FILE})
        file(APPEND ${QRC_FILE} "        <file alias=\"${_qt_id}\">${QM_FILE}</file>\n")
    endforeach()

    file(APPEND ${QRC_FILE} "    </qresource>\n</RCC>\n")

    qt5_add_resources(PROJECT_RESOURCES
        ${RESOURCE_FILES}
        ${QRC_FILE}
    )

    add_executable(${PROJECT_NAME}
        ${PROJECT_SOURCES}
        ${PROJECT_RESOURCES}
    )
endif()

target_include_directories(${PROJECT_NAME} PUBLIC
    ${CMAKE_SOURCE_DIR}/libs/dsk_tools/include/
)

target_link_libraries(${PROJECT_NAME} PRIVATE
    Qt${QT_VERSION_MAJOR}::Widgets
    ${SDL2_LIBRARIES}
    dsk_tools
)

if(QT_VERSION_MAJOR EQUAL 6)
    target_link_libraries(${PROJECT_NAME} PRIVATE
        Qt${QT_VERSION_MAJOR}::OpenGLWidgets
    )
endif()

# Different platforms setup

if(APPLE)
    if(${QT_VERSION} VERSION_LESS 6.1.0)
      set(BUNDLE_ID_OPTION MACOSX_BUNDLE_GUI_IDENTIFIER com.example.${PROJECT_NAME})
    endif()

    set(CMAKE_OSX_DEPLOYMENT_TARGET "10.15" CACHE STRING "Minimum macOS deployment version" FORCE)
    set(CMAKE_OSX_ARCHITECTURES "x86_64;arm64" CACHE STRING "Build universal binary for macOS" FORCE)
    set(ICON_FILE "${CMAKE_CURRENT_SOURCE_DIR}/resources/files/ecat.icns")
    set(PLIST_FILE "${CMAKE_CURRENT_SOURCE_DIR}/resources/files/Info.plist")
    set_target_properties(${PROJECT_NAME} PROPERTIES
        ${BUNDLE_ID_OPTION}
        MACOSX_BUNDLE_BUNDLE_VERSION ${PROJECT_VERSION}
        MACOSX_BUNDLE_SHORT_VERSION_STRING ${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR}
        MACOSX_BUNDLE TRUE
        MACOSX_BUNDLE_ICON_FILE "ecat.icns"
        MACOSX_BUNDLE_INFO_PLIST "${PLIST_FILE}"
    )
  target_sources(${PROJECT_NAME} PRIVATE ${ICON_FILE})
  add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_BUNDLE_DIR:${PROJECT_NAME}>/Contents/Resources"
    COMMAND ${CMAKE_COMMAND} -E copy "${ICON_FILE}" "$<TARGET_BUNDLE_DIR:${PROJECT_NAME}>/Contents/Resources/"
  )
endif()

set_target_properties(${PROJECT_NAME} PROPERTIES
    WIN32_EXECUTABLE TRUE
)

include(GNUInstallDirs)

# Passing version to the application

set(CMAKE_CONFIGURE_FILE_CONTENT_NEWLINE_STYLE UNIX)
configure_file (globals.h.in ${CMAKE_CURRENT_SOURCE_DIR}/globals.h @ONLY NEWLINE_STYLE LF)

install(TARGETS ${PROJECT_NAME}
    BUNDLE DESTINATION .
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

if(QT_VERSION_MAJOR EQUAL 6)
    qt_finalize_executable(${PROJECT_NAME})
endif()
