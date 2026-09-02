@echo off
SETLOCAL ENABLEEXTENSIONS

rem Regenerates the .ts files via the update_translations cmake target.
rem The build directory must be the one configured by the IDE, and the command
rem has to run on the same platform where CMakeLists.txt was preprocessed.
rem
rem Usage: update_translations.bat [build dir relative to ../src]

rem Run lupdate/lupdate-pro at the caller's privilege level so Windows UAC
rem installer-detection (triggered by "update" in the filename) does not
rem pop "allow ... to make changes to your device" prompts.
SET __COMPAT_LAYER=RunAsInvoker
rem The line above fixes the prompt only for this batch. To also silence it
rem when running lupdate from Qt Creator, set a permanent per-user (HKCU)
rem compatibility layer once in PowerShell (adjust the path on Qt upgrade):
rem   $layers = 'HKCU:\Software\Microsoft\Windows NT\CurrentVersion\AppCompatFlags\Layers'
rem   New-Item -Path $layers -Force | Out-Null
rem   Set-ItemProperty -Path $layers -Name "C:\DEV\Qt\6.11.2\mingw_64\bin\lupdate.exe"     -Value "~ RUNASINVOKER"
rem   Set-ItemProperty -Path $layers -Name "C:\DEV\Qt\6.11.2\mingw_64\bin\lupdate-pro.exe" -Value "~ RUNASINVOKER"

cd /d "%~dp0"

SET "BUILD_DIR=%~1"
if not defined BUILD_DIR SET "BUILD_DIR=build\Desktop_Qt_6_8_3_MinGW_64_bit-Debug"

if not exist "..\src\%BUILD_DIR%" (
    echo ERROR: Build directory doesn't exist: ..\src\%BUILD_DIR%
    echo Pass the right one as an argument, e.g.:
    echo    update_translations.bat build\Desktop_Qt_6_11_2_MinGW_64_bit-Debug
    exit /b 1
)

cd ..\src
cmake.exe --build "./%BUILD_DIR%" --target update_translations || exit /b 1

ENDLOCAL
