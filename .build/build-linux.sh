#!/bin/bash

ARCHITECTURE="x86_64"
PLATFORM="linux"
QT_PATH=~/Qt/6.8.1/gcc_64
LINUXDEPLOYQT="~/Downloads/linuxdeployqt-continuous-x86_64.AppImage"

BUILD_DIR="./build/${PLATFORM}-${ARCHITECTURE}"

VERSION=`cat ../src/globals.h | grep 'PROJECT_VERSION' | awk '{printf $3}' | tr -d '"'`

RELEASE_DIR="./release/mfmtools-${VERSION}-${PLATFORM}-${ARCHITECTURE}.AppDir"

cmake -DCMAKE_PREFIX_PATH=${QT_PATH} -S ../src -B ${BUILD_DIR} -G Ninja

CWD=$(pwd)
cd ${BUILD_DIR}
ninja
cd ${CWD}

mkdir -p ${RELEASE_DIR}
cp -r ./.linux/mfmtools.AppDir/* ${RELEASE_DIR}
cp "${BUILD_DIR}/MFMTools" "${RELEASE_DIR}/usr/bin/"
cp -r ../deploy/* "${RELEASE_DIR}/usr/bin/"
cp ${BUILD_DIR}/languages/*.qm "${RELEASE_DIR}/usr/bin/languages"

cd release

export VERSION=${VERSION}-${PLATFORM}
exec ${LINUXDEPLOYQT} ../${RELEASE_DIR}/usr/share/applications/mfmtools.desktop -verbose=2 -appimage -no-translations -qmake=${QT_PATH}/bin/qmake

cd $CWD
