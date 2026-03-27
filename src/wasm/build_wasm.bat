@echo off
REM Build script for eCat3 WASM version (Windows)
REM Prerequisites: Emscripten SDK installed and activated (emsdk_env.bat)
REM
REM Usage: build_wasm.bat [clean]
REM
REM Output goes to deploy-wasm\ in the project root.

setlocal enabledelayedexpansion

set "SCRIPT_DIR=%~dp0"
set "PROJECT_ROOT=%SCRIPT_DIR%..\.."
set "SRC_DIR=%PROJECT_ROOT%\src"
set "DEPLOY_DIR=%PROJECT_ROOT%\deploy"
set "BUILD_DIR=%PROJECT_ROOT%\build-wasm"
set "OUTPUT_DIR=%PROJECT_ROOT%\deploy-wasm"

REM Check for Emscripten
where emcmake >nul 2>&1
if %ERRORLEVEL% neq 0 (
    echo Error: emcmake not found. Please install and activate the Emscripten SDK:
    echo   git clone https://github.com/emscripten-core/emsdk.git
    echo   cd emsdk ^&^& emsdk install latest ^&^& emsdk activate latest
    echo   emsdk_env.bat
    exit /b 1
)

REM Clean build if requested
if "%1"=="clean" (
    echo Cleaning build directory...
    if exist "%BUILD_DIR%" rmdir /s /q "%BUILD_DIR%"
    if exist "%OUTPUT_DIR%" rmdir /s /q "%OUTPUT_DIR%"
)

REM Step 1: Build WASM binary
echo === Building WASM binary ===
call emcmake cmake -B "%BUILD_DIR%" -S "%SRC_DIR%\wasm" -DCMAKE_BUILD_TYPE=Release
if %ERRORLEVEL% neq 0 (
    echo CMake configuration failed.
    exit /b 1
)

cmake --build "%BUILD_DIR%" --parallel
if %ERRORLEVEL% neq 0 (
    echo Build failed.
    exit /b 1
)

REM Step 2: Package machine assets
echo.
echo === Packaging machine assets ===
if not exist "%OUTPUT_DIR%" mkdir "%OUTPUT_DIR%"
python "%SRC_DIR%\wasm\package_machines.py" "%DEPLOY_DIR%" "%OUTPUT_DIR%"
if %ERRORLEVEL% neq 0 (
    echo Asset packaging failed.
    exit /b 1
)

REM Step 3: Copy WASM output files
echo.
echo === Copying output files ===
copy /y "%BUILD_DIR%\ecat3.js" "%OUTPUT_DIR%\" >nul
copy /y "%BUILD_DIR%\ecat3.wasm" "%OUTPUT_DIR%\" >nul
copy /y "%BUILD_DIR%\ecat3.worker.js" "%OUTPUT_DIR%\" >nul 2>&1
copy /y "%SRC_DIR%\wasm\ecat_wasm.js" "%OUTPUT_DIR%\" >nul

REM Copy HTML shell
if exist "%BUILD_DIR%\ecat3.html" (
    copy /y "%BUILD_DIR%\ecat3.html" "%OUTPUT_DIR%\index.html" >nul
) else (
    copy /y "%SRC_DIR%\wasm\shell.html" "%OUTPUT_DIR%\index.html" >nul
)

echo.
echo === Build complete ===
echo Output directory: %OUTPUT_DIR%
echo.
echo To test locally, serve with proper headers:
echo   python "%SRC_DIR%\wasm\serve_wasm.py" "%OUTPUT_DIR%"
echo.
echo NOTE: SharedArrayBuffer requires these HTTP headers:
echo   Cross-Origin-Opener-Policy: same-origin
echo   Cross-Origin-Embedder-Policy: require-corp