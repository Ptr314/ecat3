#!/bin/bash

ARCHITECTURE="x86_64"
PLATFORM="macos"
QT_PATH="/usr/local/Qt-6.8.1-static"
BUILD_DIR="./build/${PLATFORM}-${ARCHITECTURE}"
RELEASE_DIR="./release"

VERSION=`cat ../src/globals.h | grep 'PROJECT_VERSION' | awk '{printf $3}' | tr -d '"'`

cmake -DCMAKE_PREFIX_PATH=${QT_PATH} -S ../src -B ${BUILD_DIR} -G Ninja

cwd=$(pwd)
cd "$BUILD_DIR"
ninja
macdeployqt MFMTools.app -dmg
cd $cwd
mkdir $RELEASE_DIR
cp ${BUILD_DIR}/MFMTools.dmg ${RELEASE_DIR}/mfmtools-${VERSION}-${PLATFORM}-${ARCHITECTURE}.dmg