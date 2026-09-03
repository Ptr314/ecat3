@ECHO OFF
SETLOCAL ENABLEEXTENSIONS

REM ---------------------------------------------------------------------------
REM Browser, canvas. Same output as build-wasm.sh.
REM
REM This produces NOT a desktop release but a deployment package: a directory
REM of static files that goes into a web root as is, plus the same directory
REM zipped. Machine configs, ROMs and disk images from deploy\ are packed into
REM per-machine .bundle archives by src\wasm\package_machines.py -- the page
REM fetches only the bundle of the machine the user picked.
REM
REM Needs Emscripten; paths are in vars-emsdk.cmd.
REM
REM Pass "clean" to wipe the build directory first.
REM ---------------------------------------------------------------------------

cd /d "%~dp0"
call "%~dp0vars-emsdk.cmd" || exit /b 1

SET "_CLEAN=%~1"
SET _PLATFORM=web
SET _BUILD_DIR=.\build\%_PLATFORM%

REM emsdk ships its own Python and points EMSDK_PYTHON at it, but does not put
REM it on PATH. A bare "python" on Windows usually hits the Microsoft Store
REM stub instead, which exits without doing anything.
SET "_PYTHON=%EMSDK_PYTHON%"
if not defined _PYTHON SET "_PYTHON=python"
"%_PYTHON%" --version >nul 2>&1 || (
    echo ERROR: no working Python ^("%_PYTHON%"^); package_machines.py needs it.
    exit /b 1
)

call "%~dp0win-common.cmd" version "..\VERSION" || exit /b 1

SET _RELEASE_NAME=ecat-%_VERSION%-%_PLATFORM%
SET _RELEASE_DIR=.\release\%_RELEASE_NAME%

echo Building eCat3 %_VERSION% for the browser (Emscripten)

if /I "%_CLEAN%"=="clean" if exist "%_BUILD_DIR%" rmdir /s /q "%_BUILD_DIR%"
call "%~dp0win-common.cmd" checkgen "%_BUILD_DIR%" Ninja || exit /b 1

REM Always reconfigure and rebuild: cmake and ninja work out what actually
REM changed, so a stale module never reaches the package.
call emcmake cmake -S ../src/wasm -B "%_BUILD_DIR%" -G Ninja -DCMAKE_BUILD_TYPE=Release || exit /b 1
cmake --build "%_BUILD_DIR%" || exit /b 1

if not exist "%_BUILD_DIR%\ecat3.wasm" (
    echo ERROR: "%_BUILD_DIR%\ecat3.wasm" not found.
    exit /b 1
)

call "%~dp0win-common.cmd" reset "%_RELEASE_DIR%" || exit /b 1

REM The emulator itself.
copy /y "%_BUILD_DIR%\ecat3.js"   "%_RELEASE_DIR%" >nul || exit /b 1
copy /y "%_BUILD_DIR%\ecat3.wasm" "%_RELEASE_DIR%" >nul || exit /b 1
REM Older Emscripten pthread builds emit a separate worker; newer ones inline
REM it. Copy it when it is there, do not fail when it is not.
if exist "%_BUILD_DIR%\ecat3.worker.js" copy /y "%_BUILD_DIR%\ecat3.worker.js" "%_RELEASE_DIR%" >nul

REM The page.
copy /y "..\src\wasm\shell.html"   "%_RELEASE_DIR%\index.html" >nul || exit /b 1
copy /y "..\src\wasm\ecat_wasm.js" "%_RELEASE_DIR%" >nul || exit /b 1

REM Machine configs, ROMs and disk images, one .bundle per machine plus
REM machines.json listing them for the page.
"%_PYTHON%" "..\src\wasm\package_machines.py" "..\deploy" "%_RELEASE_DIR%" || exit /b 1

REM How to put this on a server.
copy /y ".\web\README.md"          "%_RELEASE_DIR%" >nul || exit /b 1
copy /y ".\web\nginx.conf.example" "%_RELEASE_DIR%" >nul || exit /b 1
copy /y "..\deploy\LICENSE.TXT"    "%_RELEASE_DIR%" >nul || exit /b 1
copy /y "..\deploy\COPYRIGHT.TXT"  "%_RELEASE_DIR%" >nul || exit /b 1

call "%~dp0win-common.cmd" report "%_RELEASE_DIR%"
call "%~dp0win-common.cmd" zip "%_RELEASE_DIR%" "%_RELEASE_NAME%" || exit /b 1

echo.
echo   release\%_RELEASE_NAME%.zip
echo   release\%_RELEASE_NAME%\  - goes into a web root as is
echo.
echo Serving it from the file system will not work: the page needs
echo COOP/COEP headers for SharedArrayBuffer. For a local try:
echo   "%_PYTHON%" ..\src\wasm\serve_wasm.py "%_RELEASE_DIR%"
echo.

ENDLOCAL
