@echo off

rem It is assumed that the basic installation is done using the online installer.
rem The Qt path is chosen as c:\DEV\Qt,
rem so cmake, ninja and mingw are in c:\DEV\Qt\Tools.
rem The debug version of Qt with sources is in c:\DEV\Qt\X.X.X
rem and the static version was compiled and placed in c:\DEV\Qt\X.X.X-static

SET _ROOT_QT=C:\DEV\Qt
SET _QT_VERSION=6.8.3
SET _MINGW_VERSION=mingw1310_64

SET _ROOT_CMAKE=%_ROOT_QT%\Tools\CMake_64\bin
SET _ROOT_NINJA=%_ROOT_QT%\Tools\Ninja
SET _ROOT_MINGW=%_ROOT_QT%\Tools\%_MINGW_VERSION%\bin

SET _ROOT_SRC=%_ROOT_QT%\%_QT_VERSION%\Src
SET _ROOT_BIN=%_ROOT_QT%\%_QT_VERSION%\mingw_64\bin
SET _ROOT_STATIC_BIN=%_ROOT_QT%\%_QT_VERSION%-static\bin

SET SDL2_BIN=C:\DEV\SDL2-2.32.8\x86_64-w64-mingw32\bin

SET PATH=%_ROOT_CMAKE%;%_ROOT_NINJA%;%_ROOT_MINGW%;%_ROOT_BIN%;%_ROOT_SRC%;%PATH%
