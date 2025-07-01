#!/bin/bash

QT_PATH="${HOME}/Qt-6.8.2-static-universal"

PLATFORM="macos"
APP_NAME="eCat3"
BUILD_DIR="./build/${PLATFORM}-${ARCHITECTURE}"
RELEASE_DIR="./release"
RESOURCES=./eCat3.app/Contents/Resources

VERSION=$(grep 'PROJECT_VERSION' ../src/globals.h | cut -d'"' -f2 | tr -d '\r')

cmake -DCMAKE_PREFIX_PATH=${QT_PATH} -S ../src -B ${BUILD_DIR} -G Ninja -DCMAKE_OSX_ARCHITECTURES="arm64;x86_64"

cwd=$(pwd)
cd "$BUILD_DIR"
ninja

cp -r ../../../deploy/ ${RESOURCES}
rm ${RESOURCES}/ecat.ini
mv ${RESOURCES}/.ecat.ini ${RESOURCES}/ecat.ini

${QT_PATH}/bin/macdeployqt ${APP_NAME}.app -dmg
cd $cwd
mkdir $RELEASE_DIR
cp ${BUILD_DIR}/${APP_NAME}.dmg ${RELEASE_DIR}/ecat-${VERSION}-${PLATFORM}.dmg
