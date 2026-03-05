@ECHO OFF
setlocal enabledelayedexpansion

call vars-mingw-latest.cmd

SET _ARCHITECTURE=x86_64
SET _PLATFORM=windows
SET CC=%_ROOT_MINGW%\gcc.exe

FOR /F "tokens=* USEBACKQ" %%g IN (`findstr "PROJECT_VERSION" ..\src\globals.h`) do (SET VER=%%g)
for /f "tokens=3" %%G IN ("!VER!") DO (SET V=%%G)
set _VERSION=!V:"=!

set RENDERERS=SDL2 QT OPENGL

for %%R in (!RENDERERS!) do (
    set _SUFFIX=%%R
    set _RENDERER_DEFINE=-DRENDERER_%%R=1

    SET _BUILD_DIR=.\build\!_PLATFORM!_!_ARCHITECTURE!_%%R
    SET _RELEASE_NAME="ecat-!_VERSION!-!_PLATFORM!-!_ARCHITECTURE!-%%R"
    SET _RELEASE_DIR=".\release\!_RELEASE_NAME!"

    if not exist "!_BUILD_DIR!\" (
        echo Building with renderer: %%R
        call "!_ROOT_BIN!\qt-cmake" -DCMAKE_BUILD_TYPE=Release -S ../src -B "!_BUILD_DIR!" -G Ninja !_RENDERER_DEFINE!

        cd "!_BUILD_DIR!"
        ninja
        cd ..\..\
    )

    if not exist "!_RELEASE_DIR!\" mkdir "!_RELEASE_DIR!"
    xcopy ..\deploy\* "!_RELEASE_DIR!" /E
    del "!_RELEASE_DIR!\ecat.ini" 2>nul
    ren "!_RELEASE_DIR!\.ecat.ini" ecat.ini

    if not exist "!_BUILD_DIR!\ecat3.exe" (
        echo Error: "!_BUILD_DIR!\ecat3.exe" not found.
        exit /b 1
    )
    copy "!_BUILD_DIR!\ecat3.exe" "!_RELEASE_DIR!"

    if not exist "!SDL2_BIN!\SDL2.dll" (
        echo Error: "!SDL2_BIN!\SDL2.dll" not found.
        exit /b 1
    )
    copy "!SDL2_BIN!\SDL2.dll" "!_RELEASE_DIR!"

    echo Copying Qt6Core.dll, Qt6Gui.dll, Qt6Widgets.dll from !_ROOT_BIN!
    copy "!_ROOT_BIN!\Qt6Core.dll" "!_RELEASE_DIR!"
    copy "!_ROOT_BIN!\Qt6Gui.dll" "!_RELEASE_DIR!"
    copy "!_ROOT_BIN!\Qt6Widgets.dll" "!_RELEASE_DIR!"

    mkdir "!_RELEASE_DIR!\platforms"
    echo Copying platforms\qwindows.dll from !_QT_PLUGINS!
    copy "!_QT_PLUGINS!\platforms\qwindows.dll" "!_RELEASE_DIR!\platforms"

    mkdir "!_RELEASE_DIR!\styles"
    echo Copying styles\qmodernwindowsstyle.dll from !_QT_PLUGINS!
    copy "!_QT_PLUGINS!\styles\qmodernwindowsstyle.dll" "!_RELEASE_DIR!\styles"

    if "%%R"=="OPENGL" (
        echo Copying Qt6OpenGL.dll, Qt6OpenGLWidgets.dll from !_ROOT_BIN!
        copy "!_ROOT_BIN!\Qt6OpenGL.dll" "!_RELEASE_DIR!"
        copy "!_ROOT_BIN!\Qt6OpenGLWidgets.dll" "!_RELEASE_DIR!"
    )

    echo Copying libgcc_s_seh-1.dll, libstdc++-6.dll, libwinpthread-1.dll from !_ROOT_MINGW!
    copy "!_ROOT_MINGW!\libgcc_s_seh-1.dll" "!_RELEASE_DIR!"
    copy "!_ROOT_MINGW!\libstdc++-6.dll" "!_RELEASE_DIR!"
    copy "!_ROOT_MINGW!\libwinpthread-1.dll" "!_RELEASE_DIR!"


    set SEVENZIP="7z"
    !SEVENZIP! >nul 2>&1
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

    pushd "!_RELEASE_DIR!"
    !SEVENZIP! a "..\!_RELEASE_NAME!.zip" * -mx9
    popd
)