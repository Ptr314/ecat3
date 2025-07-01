@echo off
SET BUILD_DIR=Desktop_Qt_6_8_3_MinGW_64_bit-Debug
if exist "../src/build/%BUILD_DIR%" (
  cd ../src
  cmake.exe --build "./build/%BUILD_DIR%" --target update_translations
) else (
  echo "ERROR: Build directory doesn't exist: ../src/build/%BUILD_DIR%"
)