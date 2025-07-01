@echo off

rem This build script is made for making 32 bit applications for Windows 7+, 
rem so use relevant 32 bit versions of Qt and mingw

rem It is assumed that the basic installation is done using the online installer.
rem The Qt path is chosen as c:\DEV\Qt,
rem so cmake, ninja and mingw are in c:\DEV\Qt\Tools.
rem The debug version of Qt with sources is in c:\DEV\Qt\X.X.X
rem and the static version was compiled and placed in c:\DEV\Qt\X.X.X-static

SET _ROOT_QT=C:\DEV\Qt
SET _QT_VERSION=5.15.2
SET _MINGW_VERSION=mingw810_32

SET _ROOT_CMAKE=%_ROOT_QT%\Tools\CMake_64\bin
SET _ROOT_NINJA=%_ROOT_QT%\Tools\Ninja
SET _ROOT_MINGW=%_ROOT_QT%\Tools\%_MINGW_VERSION%\bin

SET _ROOT_SRC=%_ROOT_QT%\%_QT_VERSION%\Src
SET _ROOT_BIN=%_ROOT_QT%\%_QT_VERSION%\mingw81_32\bin
SET _ROOT_STATIC=%_ROOT_QT%\%_QT_VERSION%-static

SET SDL2_BIN=C:\DEV\SDL2-2.32.8\i686-w64-mingw32\bin

SET PATH=%_ROOT_CMAKE%;%_ROOT_NINJA%;%_ROOT_MINGW%;%_ROOT_BIN%;%_ROOT_SRC%;%PATH%
