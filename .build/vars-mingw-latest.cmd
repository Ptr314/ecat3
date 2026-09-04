@echo off

rem Environment for the x86_64 mingw build.
rem
rem It is assumed that the basic installation is done using the online installer.
rem The Qt path is chosen as c:\DEV\Qt,
rem so cmake, ninja and mingw are in c:\DEV\Qt\Tools.
rem The debug version of Qt with sources is in c:\DEV\Qt\X.X.X
rem and the static version was compiled and placed in c:\DEV\Qt\X.X.X-static
rem (see BUILD.md).
rem
rem _QT_PREFIX        -- regular (shared) Qt kit.
rem _QT_PREFIX_STATIC -- static Qt kit. When this directory exists,
rem                      build-win-mingw-latest.bat uses it and ships no Qt DLLs
rem                      next to the executable.

SET _ROOT_QT=C:\DEV\Qt
SET _QT_VERSION=6.11.2
SET _MINGW_VERSION=mingw1310_64

SET _ROOT_CMAKE=%_ROOT_QT%\Tools\CMake_64\bin
SET _ROOT_NINJA=%_ROOT_QT%\Tools\Ninja
SET _ROOT_MINGW=%_ROOT_QT%\Tools\%_MINGW_VERSION%\bin

SET _ROOT_SRC=%_ROOT_QT%\%_QT_VERSION%\Src
SET _QT_PREFIX=%_ROOT_QT%\%_QT_VERSION%\mingw_64
SET _QT_PREFIX_STATIC=%_ROOT_QT%\%_QT_VERSION%-static
SET _ROOT_BIN=%_QT_PREFIX%\bin
SET _QT_PLUGINS=%_QT_PREFIX%\plugins

SET SDL2_DIR=C:\DEV\SDL2-2.32.10\x86_64-w64-mingw32
SET SDL2_ROOT=%SDL2_DIR%
SET SDL2_BIN=%SDL2_DIR%\bin

SET PATH=%_ROOT_CMAKE%;%_ROOT_NINJA%;%_ROOT_MINGW%;%_ROOT_BIN%;%_ROOT_SRC%;%PATH%
