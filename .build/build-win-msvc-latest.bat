@ECHO OFF
setlocal enabledelayedexpansion

call vars-msvc-latest.cmd

SET _ARCHITECTURE=x86_64
SET _PLATFORM=windows

FOR /F "tokens=* USEBACKQ" %%g IN (`findstr "PROJECT_VERSION" ..\src\globals.h`) do (SET VER=%%g)
for /f "tokens=3" %%G IN ("!VER!") DO (SET V=%%G)
set _VERSION=!V:"=!

set RENDERERS=QT OPENGL

for %%R in (!RENDERERS!) do (
    set _SUFFIX=%%R
    set _RENDERER_DEFINE=-DRENDERER_%%R=1

    SET _BUILD_DIR=.\build\!_PLATFORM!_!_ARCHITECTURE!_msvc_%%R
    SET _RELEASE_NAME="ecat-!_VERSION!-!_PLATFORM!-!_ARCHITECTURE!-msvc-%%R"
    SET _RELEASE_DIR=".\release\!_RELEASE_NAME!"

    if not exist "!_BUILD_DIR!\" (
        echo Building with renderer: %%R
        call "!_ROOT_MSVC_CMAKE!" -S ../src -B "!_BUILD_DIR!" -G "Ninja Multi-Config" !_RENDERER_DEFINE!

        cd "!_BUILD_DIR!"
        ninja -f build-Release.ninja
        cd ..\..\
    )

    if not exist "!_RELEASE_DIR!\" mkdir "!_RELEASE_DIR!"
    xcopy ..\deploy\* "!_RELEASE_DIR!" /E
    del "!_RELEASE_DIR!\ecat.ini" 2>nul
    ren "!_RELEASE_DIR!\.ecat.ini" ecat.ini

    if not exist "!_BUILD_DIR!\Release\ecat3.exe" (
        echo Error: "!_BUILD_DIR!\Release\ecat3.exe" not found.
        exit /b 1
    )
    copy "!_BUILD_DIR!\Release\ecat3.exe" "!_RELEASE_DIR!"

    echo Copying Qt6Core.dll, Qt6Gui.dll, Qt6Widgets.dll from !_ROOT_QT!\bin
    copy "!_ROOT_QT!\bin\Qt6Core.dll" "!_RELEASE_DIR!"
    copy "!_ROOT_QT!\bin\Qt6Gui.dll" "!_RELEASE_DIR!"
    copy "!_ROOT_QT!\bin\Qt6Widgets.dll" "!_RELEASE_DIR!"

    mkdir "!_RELEASE_DIR!\platforms"
    echo Copying platforms\qwindows.dll from !_ROOT_QT!\plugins
    copy "!_ROOT_QT!\plugins\platforms\qwindows.dll" "!_RELEASE_DIR!\platforms"

    mkdir "!_RELEASE_DIR!\styles"
    echo Copying styles\qmodernwindowsstyle.dll from !_ROOT_QT!\plugins
    copy "!_ROOT_QT!\plugins\styles\qmodernwindowsstyle.dll" "!_RELEASE_DIR!\styles"

    if "%%R"=="OPENGL" (
        echo Copying Qt6OpenGL.dll, Qt6OpenGLWidgets.dll from !_ROOT_QT!\bin
        copy "!_ROOT_QT!\bin\Qt6OpenGL.dll" "!_RELEASE_DIR!"
        copy "!_ROOT_QT!\bin\Qt6OpenGLWidgets.dll" "!_RELEASE_DIR!"
    )

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