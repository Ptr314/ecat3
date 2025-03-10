#!/bin/bash

ARCHITECTURE="x86_64"
PLATFORM="macos"
QT_PATH="/usr/local/Qt-6.8.1-static"
BUILD_DIR="./build/${PLATFORM}-${ARCHITECTURE}"
RELEASE_DIR="./release"
RESOURCES=./eCat3.app/Contents/Resources

VERSION=`cat ../src/globals.h | grep 'PROJECT_VERSION' | awk '{printf $3}' | tr -d '"\n\r'`

cmake -DCMAKE_PREFIX_PATH=${QT_PATH} -S ../src -B ${BUILD_DIR} -G Ninja

cwd=$(pwd)
cd "$BUILD_DIR"
ninja

cp -r ../../../deploy/ ${RESOURCES}
rm ${RESOURCES}/ecat.ini
mv ${RESOURCES}/.ecat.ini ${RESOURCES}/ecat.ini

macdeployqt eCat3.app -dmg
cd $cwd
mkdir $RELEASE_DIR
cp ${BUILD_DIR}/eCat3.dmg ${RELEASE_DIR}/ecat-${VERSION}-${PLATFORM}-${ARCHITECTURE}.dmg
