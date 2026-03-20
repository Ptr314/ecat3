#!/bin/bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
REPO_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"

QT_PATH="${HOME}/Qt-6.8.2-static-universal"

PLATFORM="macos"
ARCHITECTURE="universal"
APP_NAME="eCat3"

RELEASE_DIR="${SCRIPT_DIR}/release"
mkdir -p "${RELEASE_DIR}"

APP_VERSION=$(grep 'PROJECT_VERSION' "${REPO_DIR}/src/globals.h" | cut -d'"' -f2 | tr -d '\r')

RENDERERS=("qt" "opengl")

for RENDERER in "${RENDERERS[@]}"; do
  echo "Building ${RENDERER} version..."
  BUILD_DIR="${SCRIPT_DIR}/build/${PLATFORM}-${ARCHITECTURE}-${RENDERER}"
  RENDERER_UPPER=$(echo "$RENDERER" | tr '[:lower:]' '[:upper:]')

  cmake -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_PREFIX_PATH="${QT_PATH}" \
        -DCMAKE_OSX_ARCHITECTURES="arm64;x86_64" \
        -DRENDERER_${RENDERER_UPPER}=1 \
        -S "${REPO_DIR}/src" \
        -B "${BUILD_DIR}" \
        -G Ninja

  cmake --build "${BUILD_DIR}"

  # Copy deploy data into app bundle Resources
  RESOURCES="${BUILD_DIR}/${APP_NAME}.app/Contents/Resources"
  cp -r "${REPO_DIR}/deploy/" "${RESOURCES}"
  rm -f "${RESOURCES}/ecat.ini"
  mv "${RESOURCES}/.ecat.ini" "${RESOURCES}/ecat.ini"

  # Bundle SDL2 dylib
  APP_BINARY="${BUILD_DIR}/${APP_NAME}.app/Contents/MacOS/${APP_NAME}"
  FRAMEWORKS_DIR="${BUILD_DIR}/${APP_NAME}.app/Contents/Frameworks"
  SDL2_LIB=$(otool -L "${APP_BINARY}" | grep libSDL2 | awk '{print $1}' || true)
  if [ -n "${SDL2_LIB}" ]; then
    mkdir -p "${FRAMEWORKS_DIR}"
    cp "${SDL2_LIB}" "${FRAMEWORKS_DIR}/"
    install_name_tool -change "${SDL2_LIB}" \
        "@executable_path/../Frameworks/$(basename "${SDL2_LIB}")" \
        "${APP_BINARY}"
  fi

  # Create DMG
  DMG_NAME="ecat-${APP_VERSION}-${PLATFORM}-${ARCHITECTURE}-${RENDERER}.dmg"
  hdiutil create -volname "${APP_NAME}" \
      -srcfolder "${BUILD_DIR}/${APP_NAME}.app" \
      -ov -format UDZO \
      "${RELEASE_DIR}/${DMG_NAME}"
done