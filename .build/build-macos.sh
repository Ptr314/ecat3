#!/bin/bash

QT_PATH="${HOME}/Qt-6.8.2-static-universal"

PLATFORM="macos"
ARCHITECTURE="universal"
APP_NAME="eCat3"

RESOURCES=./eCat3.app/Contents/Resources
RELEASE_DIR="./release"
mkdir $RELEASE_DIR

APP_VERSION=$(grep 'PROJECT_VERSION' ../src/globals.h | cut -d'"' -f2 | tr -d '\r')

RENDERERS=("qt" "opengl")

for RENDERER in "${RENDERERS[@]}"; do
  echo "Building ${RENDERER} version..."
  BUILD_DIR="./build/${PLATFORM}-${ARCHITECTURE}-${RENDERER}"

  RENDERER_UPPER=$(echo "$RENDERER" | tr '[:lower:]' '[:upper:]')

  cmake -DCMAKE_PREFIX_PATH=${QT_PATH} -S ../src -B ${BUILD_DIR} -G Ninja -DCMAKE_OSX_ARCHITECTURES="arm64;x86_64" -DRENDERER_${RENDERER_UPPER}=1

  cwd=$(pwd)
  cd "${BUILD_DIR}"
  ninja

  cp -r ../../../deploy/ ${RESOURCES}
  rm ${RESOURCES}/ecat.ini
  mv ${RESOURCES}/.ecat.ini ${RESOURCES}/ecat.ini

  ${QT_PATH}/bin/macdeployqt ${APP_NAME}.app -dmg
  cd $cwd
  cp ${BUILD_DIR}/${APP_NAME}.dmg ${RELEASE_DIR}/ecat-${APP_VERSION}-${PLATFORM}-${ARCHITECTURE}-${RENDERER}.dmg
done