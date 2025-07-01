@ECHO OFF

call vars-mingw-latest.cmd

SET _ARCHITECTURE=x86_64
SET _PLATFORM=windows
SET _BUILD_DIR=.\build\%_PLATFORM%_%_ARCHITECTURE%
SET CC=%_ROOT_MINGW%\gcc.exe

FOR /F "tokens=* USEBACKQ" %%g IN (`findstr "PROJECT_VERSION" ..\src\globals.h`) do (SET VER=%%g)
for /f "tokens=3" %%G IN ("%VER%") DO (SET V=%%G)
set _VERSION=%V:"=%

SET _RELEASE_NAME="ecat-%_VERSION%-%_PLATFORM%-%_ARCHITECTURE%"
SET _RELEASE_DIR=".\release\%_RELEASE_NAME%"

if not exist %_BUILD_DIR%\ (
    call "%_ROOT_STATIC_BIN%\qt-cmake" -S ../src -B "%_BUILD_DIR%" -G Ninja

    cd "%_BUILD_DIR%"
    ninja

    cd ..\..\
)

mkdir "%_RELEASE_DIR%"

xcopy ..\deploy\* "%_RELEASE_DIR%" /E
del %_RELEASE_DIR%\ecat.ini
ren %_RELEASE_DIR%\.ecat.ini ecat.ini

if not exist "%_BUILD_DIR%\ecat3.exe" (
    echo Error: "%_BUILD_DIR%\ecat3.exe" not found.
    exit /b 1
)
copy "%_BUILD_DIR%\ecat3.exe" "%_RELEASE_DIR%"

if not exist "%SDL2_BIN%\SDL2.dll" (
    echo Error: "%SDL2_BIN%\SDL2.dll" not found.
    exit /b 1
)
copy "%SDL2_BIN%\SDL2.dll" "%_RELEASE_DIR%"

set SEVENZIP="7z"
%SEVENZIP% >nul 2>&1
if errorlevel 9009 (
    if exist "C:\Program Files\7-Zip\7z.exe" (
        set SEVENZIP="C:\Program Files\7-Zip\7z.exe"
    ) else if exist "C:\Program Files (x86)\7-Zip\7z.exe" (
        set SEVENZIP="C:\Program Files (x86)\7-Zip\7z.exe"
    ) else (
        echo ERROR: 7z.exe not found. Please install 7-Zip or add it to PATH.
        exit /b 1
    )
)

pushd "%_RELEASE_DIR%"
%SEVENZIP% a "..\%_RELEASE_NAME%.zip" * -mx9
popd

