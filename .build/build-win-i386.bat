@ECHO OFF
REM ---------------------------------------------------------------------------
REM Release build, i386, for Windows XP: Qt 5.6.3 + mingw 4.9.2.
REM Builds the SDL2 and Qt renderers, one release archive each.
REM
REM Pass "clean" to wipe the build directories first.
REM ---------------------------------------------------------------------------

call "%~dp0win-build-qt5.cmd" vars-mingw-qt5.6.cmd windows %1 || exit /b 1
