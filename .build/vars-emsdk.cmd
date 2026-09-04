@ECHO OFF

rem Toolchain for the browser build: Emscripten from emsdk.
rem
rem emsdk_env.bat puts emcc/emcmake and node into PATH, and points
rem EMSDK_PYTHON at its own Python -- that is what runs package_machines.py,
rem so no separate Python install is needed. (A bare "python" on Windows
rem usually hits the Microsoft Store stub, which is why build-wasm.cmd
rem prefers EMSDK_PYTHON.)
rem
rem CMake and Ninja still come from the Qt tools, as in the other vars-*.cmd
rem files; Qt itself is not used by this build at all.

SET _ROOT_EMSDK=C:\DEV\emsdk
SET _ROOT_QT=C:\DEV\Qt

if not exist "%_ROOT_EMSDK%\emsdk_env.bat" (
    echo ERROR: emsdk not found in "%_ROOT_EMSDK%".
    echo   git clone https://github.com/emscripten-core/emsdk.git
    echo   cd emsdk ^&^& emsdk install latest ^&^& emsdk activate latest
    exit /b 1
)

call "%_ROOT_EMSDK%\emsdk_env.bat" >nul

SET _ROOT_CMAKE=%_ROOT_QT%\Tools\CMake_64\bin
SET _ROOT_NINJA=%_ROOT_QT%\Tools\Ninja

SET PATH=%_ROOT_CMAKE%;%_ROOT_NINJA%;%PATH%
