@ECHO OFF
SETLOCAL ENABLEEXTENSIONS

REM ---------------------------------------------------------------------------
REM The software library for the site: ..\library in one zip, the same archive
REM pack_library.py makes. Inside is the library\ directory itself, so the
REM archive unpacked into the repository root gives the tests their images.
REM library\README.md stays out (it is about the repository); wav2uknc.py lives
REM in tools\ and goes next to the UKNC images, where the machine descriptions
REM point to it.
REM
REM Result: release\ecat3-library.zip, for https://ecat.emuverse.ru/library/.
REM Needs 7-Zip (see win-common.cmd find7z).
REM ---------------------------------------------------------------------------

cd /d "%~dp0"

SET _NAME=ecat3-library
REM A copy to pack: the archive needs library\ at its root and a file from
REM tools\ inside. It is made in %TEMP%, not in release\: a synced folder
REM (Google Drive) holds the fresh files, and the copy could not be removed.
SET "_WORK=%TEMP%\ecat3-pack-library"
SET "_STAGE=%_WORK%\%_NAME%"

REM Its own call: the "zip" step below is silenced (it names the %TEMP% path)
call "%~dp0win-common.cmd" find7z || exit /b 1
call "%~dp0win-common.cmd" reset "%_STAGE%" || exit /b 1
xcopy "..\library\*" "%_STAGE%\library\" /E /I /Y /Q >nul || goto :fail
if exist "%_STAGE%\library\README.md" del /q "%_STAGE%\library\README.md"

REM library\ is not in git: a fresh clone has only its README.md.
dir /s /b /a-d "%_STAGE%\library" >nul 2>&1 || (
    echo ERROR: ..\library has no images. It is not in git: fill it from the previous archive.
    goto :fail
)

if not exist "%_STAGE%\library\uknc" mkdir "%_STAGE%\library\uknc"
copy /y "..\tools\uknc-tape\wav2uknc.py" "%_STAGE%\library\uknc\" >nul || goto :fail

call "%~dp0win-common.cmd" zip "%_STAGE%" "%_NAME%" >nul || goto :fail
if not exist ".\release" mkdir ".\release"
move /y "%_WORK%\%_NAME%.zip" ".\release\%_NAME%.zip" >nul || goto :fail
rmdir /s /q "%_WORK%"
echo Packed: release\%_NAME%.zip
exit /b 0

:fail
if exist "%_WORK%" rmdir /s /q "%_WORK%"
exit /b 1
