@ECHO OFF
REM ---------------------------------------------------------------------------
REM Release build, i386, for Windows XP: Qt 5.6.3 + mingw 4.9.2.
REM Builds the SDL2 renderer; with "all" the Qt one too, one archive each.
REM
REM Arguments: "clean" wipes the build directories first, "all" adds Qt.
REM ---------------------------------------------------------------------------

call "%~dp0win-build-qt5.cmd" vars-mingw-qt5.6.cmd windows %* || exit /b 1
