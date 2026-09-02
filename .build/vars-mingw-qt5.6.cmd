@echo off

rem Environment for the i386 / Windows XP build: Qt 5.6.3 + mingw 4.9.2.
rem Qt 5.6.3 is rebuilt from sources with the -prefix below (see BUILD.md);
rem the release ships Qt5*.dll and the mingw runtime next to the executable.

SET _ROOT_QT=C:\DEV\Qt
SET _QT_VERSION=5.6.3
SET _MINGW_VERSION=mingw492_32

SET _ROOT_CMAKE=%_ROOT_QT%\Tools\CMake_64\bin
SET _ROOT_NINJA=%_ROOT_QT%\Tools\Ninja
SET _ROOT_MINGW=%_ROOT_QT%\Tools\%_MINGW_VERSION%\bin

SET _ROOT_SRC=%_ROOT_QT%\%_QT_VERSION%\Src
SET _QT_PREFIX=%_ROOT_QT%\%_QT_VERSION%\%_MINGW_VERSION%
SET _ROOT_BIN=%_QT_PREFIX%\bin
SET _QT_PLUGINS=%_QT_PREFIX%\plugins

rem mingw 4.9.2 (32-bit, dwarf2 exception model)
SET _MINGW_RUNTIME=libgcc_s_dw2-1.dll

SET SDL2_ROOT=C:\DEV\SDL2-2.32.10\i686-w64-mingw32
SET SDL2_BIN=%SDL2_ROOT%\bin

SET PATH=%_ROOT_CMAKE%;%_ROOT_NINJA%;%_ROOT_MINGW%;%_ROOT_BIN%;%_ROOT_SRC%;%PATH%
