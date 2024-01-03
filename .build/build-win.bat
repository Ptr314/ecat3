@echo off

SET QT_ROOT=C:\DEV\Qt\6.6.1\mingw_64\bin
SET SDL2_ROOT=C:\DEV\SDL2-2.28.3\x86_64-w64-mingw32\bin

FOR /F "tokens=* USEBACKQ" %%g IN (`findstr "PROJECT_VERSION" ..\src\globals.h`) do (SET VER=%%g)
for /f "tokens=3" %%G IN ("%VER%") DO (SET V=%%G)
set V=%V:"=%

cd .\releases\windows

xcopy ..\..\..\deploy\* . /E
copy ..\..\..\build-release\eCat3.exe .

copy "%QT_ROOT%\libgcc_s_seh-1.dll" .
copy "%QT_ROOT%\libstdc++-6.dll" .
copy "%QT_ROOT%\libwinpthread-1.dll" .
copy "%SDL2_ROOT%\SDL2.dll" .

copy /Y .ecat.ini ecat.ini

"C:\Program Files\7-Zip\7z.exe" a -x!.* "eCat-%V%-win-x86_64.zip" *