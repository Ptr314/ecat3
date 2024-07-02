@echo off

SET QT_BIN=C:\DEV\Qt\6.7.2\mingw_64\bin
SET SDL2_BIN=C:\DEV\SDL2-2.30.4\x86_64-w64-mingw32\bin

FOR /F "tokens=* USEBACKQ" %%g IN (`findstr "PROJECT_VERSION" ..\src\globals.h`) do (SET VER=%%g)
for /f "tokens=3" %%G IN ("%VER%") DO (SET V=%%G)
set V=%V:"=%

cd .\releases\windows

xcopy ..\..\..\deploy\* . /E
copy ..\..\..\build-release\eCat3.exe .

copy "%QT_BIN%\libgcc_s_seh-1.dll" .
copy "%QT_BIN%\libstdc++-6.dll" .
copy "%QT_BIN%\libwinpthread-1.dll" .
copy "%SDL2_BIN%\SDL2.dll" .

copy /Y .ecat.ini ecat.ini

"C:\Program Files\7-Zip\7z.exe" a -x!.* "eCat-%V%-win-x86_64.zip" *