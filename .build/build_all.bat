@ECHO OFF
SETLOCAL ENABLEEXTENSIONS ENABLEDELAYEDEXPANSION

REM ---------------------------------------------------------------------------
REM Builds every Windows release plus the browser package, one after another:
REM
REM   build-win-i386.bat           Windows XP,  i386,   Qt 5.6.3 + mingw 4.9.2
REM   build-win-7.bat              Windows 7,   i386,   Qt 5.15.2 + mingw 8.1
REM   build-win-mingw-latest.bat   Windows 10+, x86_64, Qt 6 + mingw
REM   build-win-msvc-latest.bat    Windows 10+, x86_64, Qt 6 + MSVC
REM   build-wasm.cmd               browser
REM
REM Each script runs in its own cmd.exe, so the toolchain one of them puts on
REM PATH never reaches the next. A failed build does not stop the rest: the
REM summary at the end lists what failed, and the exit code is 1 then.
REM
REM Arguments, in any order:
REM   clean      wipe the build directories first
REM   headless   also build the console executable of the two x86_64 targets
REM   mcp        additionally build the MCP variants of the two x86_64 targets
REM              (OpenGL only, archives get the -mcp suffix)
REM ---------------------------------------------------------------------------

SET "_HERE=%~dp0"
SET "_CLEAN="
SET "_HEADLESS="
SET "_MCP=0"
for %%A in (%*) do (
    SET "_KNOWN="
    if /I "%%A"=="clean"    (SET "_CLEAN=clean" & SET "_KNOWN=1")
    if /I "%%A"=="headless" (SET "_HEADLESS=headless" & SET "_KNOWN=1")
    if /I "%%A"=="mcp"      (SET "_MCP=1" & SET "_KNOWN=1")
    if not defined _KNOWN (
        echo ERROR: unknown argument "%%A". Allowed: clean, headless, mcp
        exit /b 1
    )
)

SET _N=0
SET _FAILED=0
SET "_STARTED=%DATE% %TIME%"

call :step build-win-i386.bat         "%_CLEAN%"
call :step build-win-7.bat            "%_CLEAN%"
call :step build-win-mingw-latest.bat "%_HEADLESS% %_CLEAN%"
call :step build-win-msvc-latest.bat  "%_HEADLESS% %_CLEAN%"
if "%_MCP%"=="1" (
    REM The build directories were already cleaned by the passes above
    call :step build-win-mingw-latest.bat "mcp"
    call :step build-win-msvc-latest.bat  "mcp"
)
call :step build-wasm.cmd             "%_CLEAN%"

echo.
echo ###########################################################################
echo ### Summary
echo ###   started  %_STARTED%
echo ###   finished %DATE% %TIME%
echo ###########################################################################
for /L %%I in (1,1,%_N%) do echo   !_RESULT_%%I!
echo.

if not "%_FAILED%"=="0" (
    echo %_FAILED% build^(s^) failed.
    exit /b 1
)
echo All builds succeeded, archives are in "%_HERE%release".
exit /b 0

REM ---------------------------------------------------------------------------
REM   call :step <script> "<arguments>"
:step
SET "_SCRIPT=%~1"
SET "_ARGS=%~2"
SET "_LABEL=%_SCRIPT% %_ARGS%"
SET "_T0=%TIME%"

echo.
echo ###########################################################################
echo ### %_LABEL%
echo ###   started %_T0%
echo ###########################################################################

cmd /c ""%_HERE%%_SCRIPT%" %_ARGS%"
if errorlevel 1 (
    SET "_STATUS=FAILED"
    SET /A _FAILED+=1
) else (
    SET "_STATUS=ok    "
)

SET /A _N+=1
SET "_RESULT_%_N%=!_STATUS!  %_T0% - %TIME%  %_LABEL%"
echo.
echo ### %_LABEL%: !_STATUS!
exit /b 0
