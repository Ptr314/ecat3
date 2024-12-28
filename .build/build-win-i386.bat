@ECHO OFF

CALL vars-mingw-qt5.15.cmd

SET _ARCHITECTURE=i386
SET _PLATFORM=windows
SET _BUILD_DIR=.\build\%_PLATFORM%_%_ARCHITECTURE%

FOR /F "tokens=* USEBACKQ" %%g IN (`findstr "PROJECT_VERSION" ..\src\globals.h`) do (SET VER=%%g)
for /f "tokens=3" %%G IN ("%VER%") DO (SET V=%%G)
set _VERSION=%V:"=%

SET _RELEASE_DIR=".\release\ecat-%_VERSION%-%_PLATFORM%-%_ARCHITECTURE%"

if not exist %_BUILD_DIR%\ (
    set CC=%_ROOT_MINGW%\gcc.exe
    cmake -DCMAKE_PREFIX_PATH="%_ROOT_STATIC%" -S ../src -B "%_BUILD_DIR%" -G Ninja

    cd "%_BUILD_DIR%"
    ninja

    cd ..\..\
)

mkdir "%_RELEASE_DIR%"

xcopy ..\deploy\* "%_RELEASE_DIR%" /E
del %_RELEASE_DIR%\ecat.ini
ren %_RELEASE_DIR%\.ecat.ini ecat.ini

copy "%_BUILD_DIR%\ecat3.exe" "%_RELEASE_DIR%"
copy "%SDL2_BIN%\SDL2.dll" "%_RELEASE_DIR%"

rem copy "%_QT_PATH%\bin\Qt5Core.dll" "%_RELEASE_DIR%"
rem copy "%_QT_PATH%\bin\Qt5Gui.dll" "%_RELEASE_DIR%"
rem copy "%_QT_PATH%\bin\Qt5Widgets.dll" "%_RELEASE_DIR%"

copy "%_ROOT_MINGW%\libgcc_s_dw2-1.dll" "%_RELEASE_DIR%"
copy "%_ROOT_MINGW%\libstdc++-6.dll" "%_RELEASE_DIR%"
copy "%_ROOT_MINGW%\libwinpthread-1.dll" "%_RELEASE_DIR%"

rem mkdir "%_RELEASE_DIR%\platforms"
rem copy "%_QT_PATH%\plugins\platforms\qwindows.dll" "%_RELEASE_DIR%\platforms"



