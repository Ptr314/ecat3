@ECHO OFF
SETLOCAL ENABLEEXTENSIONS

REM ---------------------------------------------------------------------------
REM Release build, x86_64, MSVC. One release archive per renderer.
REM
REM If vars-msvc-latest.cmd points at an existing static Qt prefix
REM (_QT_PREFIX_STATIC), that kit is used and the result is a single
REM self-contained exe: no Qt*.dll, no MSVC runtime redistributable (Qt is
REM configured with -static-runtime) and no platforms\ / styles\ directories.
REM Otherwise a shared build is produced and a minimal set of DLLs is copied
REM next to the executable.
REM
REM Pass "clean" to wipe the build directories first.
REM ---------------------------------------------------------------------------

cd /d "%~dp0"
call "%~dp0vars-msvc-latest.cmd" || exit /b 1

SET "_CLEAN=%~1"
SET _ARCHITECTURE=x86_64
SET _COMPILER=msvc
SET _PLATFORM=windows

SET RENDERERS=QT OPENGL

call "%~dp0win-common.cmd" version "..\src\CMakeLists.txt" || exit /b 1

if exist "%_QT_PREFIX_STATIC%\bin\qt-cmake.bat" (
    SET "_QT_KIT=%_QT_PREFIX_STATIC%"
    SET _QT_STATIC=1
) else (
    SET "_QT_KIT=%_QT_PREFIX%"
    SET _QT_STATIC=0
    echo WARNING: no static Qt in "%_QT_PREFIX_STATIC%", falling back to a shared build.
)

if not exist "%_QT_KIT%\bin\qt-cmake.bat" (
    echo ERROR: Qt not found in "%_QT_KIT%".
    exit /b 1
)

echo Building eCat3 %_VERSION% for %_PLATFORM% %_ARCHITECTURE% (%_COMPILER%, Qt %_QT_VERSION%)

for %%R in (%RENDERERS%) do call :build_one %%R || exit /b 1

ENDLOCAL
exit /b 0

REM ---------------------------------------------------------------------------
:build_one
SET _RENDERER=%~1
SET _BUILD_DIR=.\build\%_PLATFORM%_%_ARCHITECTURE%_%_COMPILER%_%_RENDERER%
SET _RELEASE_NAME=ecat-%_VERSION%-%_PLATFORM%-%_ARCHITECTURE%-%_COMPILER%-%_RENDERER%
SET _RELEASE_DIR=.\release\%_RELEASE_NAME%

echo.
echo === Renderer: %_RENDERER%

if /I "%_CLEAN%"=="clean" if exist "%_BUILD_DIR%" rmdir /s /q "%_BUILD_DIR%"
call "%~dp0win-common.cmd" checkgen "%_BUILD_DIR%" Ninja || exit /b 1

REM Plain Ninja (not Ninja Multi-Config): the release only ever needs the
REM Release configuration, and the executable then lands directly in
REM %_BUILD_DIR% instead of %_BUILD_DIR%\Release.
REM
REM Always reconfigure and rebuild: cmake and ninja work out what actually
REM changed. Previously the whole build was skipped when the directory already
REM existed, so a stale executable could be packaged into the release.
call "%_QT_KIT%\bin\qt-cmake" -S ../src -B "%_BUILD_DIR%" -G Ninja ^
     -DCMAKE_BUILD_TYPE=Release -DRENDERER_%_RENDERER%=1 || exit /b 1
cmake --build "%_BUILD_DIR%" || exit /b 1

if not exist "%_BUILD_DIR%\ecat3.exe" (
    echo ERROR: "%_BUILD_DIR%\ecat3.exe" not found.
    exit /b 1
)

call "%~dp0win-common.cmd" reset "%_RELEASE_DIR%" || exit /b 1
call "%~dp0win-common.cmd" deploy "%_RELEASE_DIR%" || exit /b 1
copy /y "%_BUILD_DIR%\ecat3.exe" "%_RELEASE_DIR%" >nul || exit /b 1

if "%_QT_STATIC%"=="1" (
    echo Static Qt build: no runtime DLLs needed.
) else (
    echo Copying Qt runtime from "%_QT_KIT%"
    copy /y "%_QT_KIT%\bin\Qt6Core.dll"    "%_RELEASE_DIR%" >nul || exit /b 1
    copy /y "%_QT_KIT%\bin\Qt6Gui.dll"     "%_RELEASE_DIR%" >nul || exit /b 1
    copy /y "%_QT_KIT%\bin\Qt6Widgets.dll" "%_RELEASE_DIR%" >nul || exit /b 1

    if /I "%_RENDERER%"=="OPENGL" (
        copy /y "%_QT_KIT%\bin\Qt6OpenGL.dll"        "%_RELEASE_DIR%" >nul || exit /b 1
        copy /y "%_QT_KIT%\bin\Qt6OpenGLWidgets.dll" "%_RELEASE_DIR%" >nul || exit /b 1
    )

    mkdir "%_RELEASE_DIR%\platforms" || exit /b 1
    copy /y "%_QT_KIT%\plugins\platforms\qwindows.dll" "%_RELEASE_DIR%\platforms\" >nul || exit /b 1

    mkdir "%_RELEASE_DIR%\styles" || exit /b 1
    copy /y "%_QT_KIT%\plugins\styles\qmodernwindowsstyle.dll" "%_RELEASE_DIR%\styles\" >nul || exit /b 1
)

call "%~dp0win-common.cmd" report "%_RELEASE_DIR%"
call "%~dp0win-common.cmd" zip "%_RELEASE_DIR%" "%_RELEASE_NAME%" || exit /b 1
exit /b 0
