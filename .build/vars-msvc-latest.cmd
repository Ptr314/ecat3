@ECHO OFF

SET _QT_VERSION=6.10.2
SET _ROOT_MSVC=C:\DEV\MSVC\msvc
SET _ROOT_QT=C:\DEV\Qt\%_QT_VERSION%\msvc2022_64-static
SET _ROOT_SRC=C:\DEV\Qt\%_QT_VERSION%\Src
SET _ROOT_STATIC_BIN=%_ROOT_QT%\bin
SET _ROOT_MSVC_CMAKE=%_ROOT_QT%\bin\qt-cmake

SET _ROOT_CMAKE=C:\DEV\Qt\Tools\CMake_64\bin
SET _ROOT_NINJA=C:\DEV\Qt\Tools\Ninja

SET PATH=%_ROOT_CMAKE%;%_ROOT_NINJA%;%_ROOT_QT%\bin;%_ROOT_SRC%;%PATH%

REM Initialize Visual Studio build environment
call "%_ROOT_MSVC%\setup_x64.bat"
