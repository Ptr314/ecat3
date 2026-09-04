@ECHO OFF
REM ---------------------------------------------------------------------------
REM Release build, i386, for Windows 7+: Qt 5.15.2 + mingw 8.1.
REM Builds the SDL2 and Qt renderers, one release archive each.
REM
REM Pass "clean" to wipe the build directories first.
REM ---------------------------------------------------------------------------

call "%~dp0win-build-qt5.cmd" vars-mingw-qt5.15.cmd windows_7 %1 || exit /b 1
