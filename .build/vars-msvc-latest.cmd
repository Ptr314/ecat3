@ECHO OFF

rem Environment for the x86_64 MSVC build.
rem
rem _QT_PREFIX        -- regular (shared) Qt installation.
rem _QT_PREFIX_STATIC -- static Qt build. When this directory exists,
rem                      build-win-msvc-latest.bat picks it and produces an exe
rem                      with no Qt DLLs and no MSVC runtime redistributable
rem                      (Qt is configured with -static-runtime).
rem                      See BUILD.md for how to build it.

SET _QT_VERSION=6.11.2
SET _ROOT_MSVC=C:\DEV\MSVC\msvc
SET _ROOT_QT=C:\DEV\Qt
SET _ROOT_SRC=%_ROOT_QT%\%_QT_VERSION%\Src

SET _QT_PREFIX=%_ROOT_QT%\%_QT_VERSION%\msvc2022_64
SET _QT_PREFIX_STATIC=%_ROOT_QT%\%_QT_VERSION%\msvc2022_64-static

SET _ROOT_CMAKE=%_ROOT_QT%\Tools\CMake_64\bin
SET _ROOT_NINJA=%_ROOT_QT%\Tools\Ninja

SET PATH=%_ROOT_CMAKE%;%_ROOT_NINJA%;%_ROOT_SRC%;%PATH%

REM Initialize Visual Studio build environment
call "%_ROOT_MSVC%\setup_x64.bat"
