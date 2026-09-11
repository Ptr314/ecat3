@ECHO OFF
SETLOCAL ENABLEEXTENSIONS

REM ---------------------------------------------------------------------------
REM Local try of the browser build: serves the package made by build-wasm.cmd
REM with the COOP/COEP headers the page needs and opens it in the browser.
REM
REM Arguments, in any order:
REM   build      rebuild the package first (runs build-wasm.cmd)
REM   clean      the same, wiping the build directory before
REM   nobrowser  only start the server, do not open the page
REM   <number>   port, 8080 by default
REM
REM Without "build" the package is built only when it is missing, so a change
REM to the sources reaches the page only with "build". The server sends
REM Cache-Control: no-store, so a reload after a rebuild never shows the old
REM module. Ctrl+C stops it.
REM
REM Opening index.html from the file system will not work, nor will a plain
REM "python -m http.server": without the headers SharedArrayBuffer is not
REM available and the page stalls on module initialisation.
REM ---------------------------------------------------------------------------

cd /d "%~dp0"

SET "_BUILD="
SET "_CLEAN="
SET "_OPEN=--open"
SET "_PORT=8080"

:args
if "%~1"=="" goto :args_done
if /I "%~1"=="build" (
    SET "_BUILD=1"
    shift
    goto :args
)
if /I "%~1"=="clean" (
    SET "_BUILD=1"
    SET "_CLEAN=clean"
    shift
    goto :args
)
if /I "%~1"=="nobrowser" (
    SET "_OPEN="
    shift
    goto :args
)
echo %~1| findstr /r /x "[0-9][0-9]*" >nul
if not errorlevel 1 (
    SET "_PORT=%~1"
    shift
    goto :args
)
echo ERROR: unknown argument "%~1".
echo Usage: serve-wasm.cmd [build^|clean] [nobrowser] [port]
exit /b 1
:args_done

call "%~dp0win-common.cmd" version "..\VERSION" || exit /b 1
SET "_RELEASE_NAME=ecat-%_VERSION%-web"
SET "_RELEASE_DIR=.\release\%_RELEASE_NAME%"

if not exist "%_RELEASE_DIR%\index.html" SET "_BUILD=1"
if defined _BUILD (
    call "%~dp0build-wasm.cmd" %_CLEAN% || exit /b 1
)
if not exist "%_RELEASE_DIR%\ecat3.wasm" (
    echo ERROR: "%_RELEASE_DIR%\ecat3.wasm" not found.
    exit /b 1
)

REM Python: the one from emsdk first, as build-wasm.cmd does (a bare "python"
REM on Windows is often the Microsoft Store stub), then the py launcher, then
REM whatever "python" is. Serving needs no Emscripten, so a missing emsdk is
REM not an error here.
SET "_PYTHON="
call "%~dp0vars-emsdk.cmd" >nul 2>&1
if defined EMSDK_PYTHON SET "_PYTHON=%EMSDK_PYTHON%"
if not defined _PYTHON (
    where py >nul 2>&1 && SET "_PYTHON=py"
)
if not defined _PYTHON SET "_PYTHON=python"
"%_PYTHON%" --version >nul 2>&1 || (
    echo ERROR: no working Python ^("%_PYTHON%"^).
    exit /b 1
)

echo.
echo Serving release\%_RELEASE_NAME%
echo   http://localhost:%_PORT%/
echo Ctrl+C stops the server.
echo.
"%_PYTHON%" "..\src\wasm\serve_wasm.py" "%_RELEASE_DIR%" %_PORT% %_OPEN%

ENDLOCAL
