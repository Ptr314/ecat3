@ECHO OFF
REM ---------------------------------------------------------------------------
REM Release build, i386, for Windows 7+: Qt 5.15.2 + mingw 8.1.
REM Builds the SDL2 renderer; with "all" the Qt one too, one archive each.
REM
REM Arguments: "clean" wipes the build directories first, "all" adds Qt.
REM ---------------------------------------------------------------------------

call "%~dp0win-build-qt5.cmd" vars-mingw-qt5.15.cmd windows_7 %* || exit /b 1
