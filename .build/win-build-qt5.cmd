@ECHO OFF
SETLOCAL ENABLEEXTENSIONS

REM ---------------------------------------------------------------------------
REM Shared body of the two i386 / Qt5 release builds (Windows XP and Windows 7).
REM Both differ only in the toolchain and the platform label, so the actual
REM steps live here and build-win-i386.bat / build-win-7.bat only pick the kit.
REM
REM   call win-build-qt5.cmd <vars file> <platform label> [clean]
REM
REM Qt 5 cannot be configured as a static build here (see BUILD.md), so a
REM minimal set of DLLs is shipped next to the exe. The mingw runtime cannot be
REM dropped -- the Qt5*.dll themselves import it, not just our exe.
REM ---------------------------------------------------------------------------

cd /d "%~dp0"

SET "_PLATFORM=%~2"
SET "_CLEAN=%~3"

call "%~dp0%~1" || exit /b 1

SET _ARCHITECTURE=i386
SET "CC=%_ROOT_MINGW%\gcc.exe"

call "%~dp0win-common.cmd" version "..\VERSION" || exit /b 1
echo Building eCat3 %_VERSION% for %_PLATFORM% %_ARCHITECTURE% (Qt %_QT_VERSION%, %_MINGW_VERSION%)

if not exist "%_QT_PREFIX%\bin\Qt5Core.dll" (
    echo ERROR: Qt not found in "%_QT_PREFIX%".
    exit /b 1
)

for %%R in (SDL2 QT) do call :build_one %%R || exit /b 1

ENDLOCAL
exit /b 0

REM ---------------------------------------------------------------------------
:build_one
SET _RENDERER=%~1
SET _BUILD_DIR=.\build\%_PLATFORM%_%_ARCHITECTURE%_%_RENDERER%
SET _RELEASE_NAME=ecat-%_VERSION%-%_PLATFORM%-%_ARCHITECTURE%-%_RENDERER%
SET _RELEASE_DIR=.\release\%_RELEASE_NAME%

echo.
echo === Renderer: %_RENDERER%

if /I "%_CLEAN%"=="clean" if exist "%_BUILD_DIR%" rmdir /s /q "%_BUILD_DIR%"
call "%~dp0win-common.cmd" checkgen "%_BUILD_DIR%" Ninja || exit /b 1

REM Always reconfigure and rebuild: cmake and ninja work out what actually
REM changed. Previously the whole build was skipped when the directory already
REM existed, so a stale executable could be packaged into the release.
cmake -S ../src -B "%_BUILD_DIR%" -G Ninja ^
      -DCMAKE_BUILD_TYPE=Release ^
      -DCMAKE_PREFIX_PATH="%_QT_PREFIX%;%SDL2_ROOT%" ^
      -DRENDERER_%_RENDERER%=1 || exit /b 1
cmake --build "%_BUILD_DIR%" || exit /b 1

if not exist "%_BUILD_DIR%\ecat3.exe" (
    echo ERROR: "%_BUILD_DIR%\ecat3.exe" not found.
    exit /b 1
)

call "%~dp0win-common.cmd" reset "%_RELEASE_DIR%" || exit /b 1
call "%~dp0win-common.cmd" deploy "%_RELEASE_DIR%" || exit /b 1
copy /y "%_BUILD_DIR%\ecat3.exe" "%_RELEASE_DIR%" >nul || exit /b 1

if /I "%_RENDERER%"=="SDL2" (
    echo Copying SDL2 runtime from "%SDL2_BIN%"
    copy /y "%SDL2_BIN%\SDL2.dll" "%_RELEASE_DIR%" >nul || exit /b 1
)

echo Copying Qt runtime from "%_QT_PREFIX%"
copy /y "%_QT_PREFIX%\bin\Qt5Core.dll"    "%_RELEASE_DIR%" >nul || exit /b 1
copy /y "%_QT_PREFIX%\bin\Qt5Gui.dll"     "%_RELEASE_DIR%" >nul || exit /b 1
copy /y "%_QT_PREFIX%\bin\Qt5Widgets.dll" "%_RELEASE_DIR%" >nul || exit /b 1

mkdir "%_RELEASE_DIR%\platforms" || exit /b 1
copy /y "%_QT_PLUGINS%\platforms\qwindows.dll" "%_RELEASE_DIR%\platforms\" >nul || exit /b 1

echo Copying mingw runtime from "%_ROOT_MINGW%"
copy /y "%_ROOT_MINGW%\%_MINGW_RUNTIME%"    "%_RELEASE_DIR%" >nul || exit /b 1
copy /y "%_ROOT_MINGW%\libstdc++-6.dll"     "%_RELEASE_DIR%" >nul || exit /b 1
copy /y "%_ROOT_MINGW%\libwinpthread-1.dll" "%_RELEASE_DIR%" >nul || exit /b 1

call "%~dp0win-common.cmd" report "%_RELEASE_DIR%"
call "%~dp0win-common.cmd" zip "%_RELEASE_DIR%" "%_RELEASE_NAME%" || exit /b 1
exit /b 0
