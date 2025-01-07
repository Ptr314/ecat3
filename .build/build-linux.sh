#!/bin/bash

ARCHITECTURE="x86_64"
PLATFORM="linux"
QT_PATH=~/Qt/6.8.1/gcc_64
LINUXDEPLOYQT="~/Downloads/linuxdeployqt-continuous-x86_64.AppImage"

BUILD_DIR="./build/${PLATFORM}-${ARCHITECTURE}"

VERSION=`cat ../src/globals.h | grep 'PROJECT_VERSION' | awk '{printf $3}' | tr -d '"\n\r'`

RELEASE_DIR="./release/ecat-${VERSION}-${PLATFORM}-${ARCHITECTURE}.AppDir"
RESOURCES=${RELEASE_DIR}/usr/share/ecat

cmake -DCMAKE_PREFIX_PATH=${QT_PATH} -S ../src -B ${BUILD_DIR} -G Ninja

CWD=$(pwd)
cd ${BUILD_DIR}
ninja
cd ${CWD}

mkdir -p ${RELEASE_DIR}/usr/bin/
cp -r ./.linux/ecat3.AppDir/* ${RELEASE_DIR}
cp "${BUILD_DIR}/eCat3" "${RELEASE_DIR}/usr/bin/"

mkdir -p ${RELEASE_DIR}/usr/share/ecat
cp -r ../deploy/* ${RESOURCES}
rm ${RESOURCES}/ecat.ini
cp ../deploy/.ecat.ini ${RESOURCES}/ecat.ini

cd release

export VERSION=${VERSION}-${PLATFORM}
exec ${LINUXDEPLOYQT} ../${RELEASE_DIR}/usr/share/applications/ecat3.desktop -verbose=2 -appimage -no-translations -qmake=${QT_PATH}/bin/qmake

cd $CWD
