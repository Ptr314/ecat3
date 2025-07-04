#!/bin/bash

ARCHITECTURE="x86_64"
PLATFORM="linux"
QT_PATH=~/Qt/6.8.2/gcc_64
LINUXDEPLOYQT=~/Downloads/linuxdeployqt-continuous-x86_64.AppImage

APP_VERSION=$(grep 'PROJECT_VERSION' ../src/globals.h | cut -d'"' -f2 | tr -d '\r')

# Массив рендереров
RENDERERS=("sdl2" "qt" "opengl")

for RENDERER in "${RENDERERS[@]}"; do
    echo "Building ${RENDERER} version..."

    BUILD_DIR="./build/${PLATFORM}-${ARCHITECTURE}-${RENDERER}"
    RELEASE_DIR="./release/ecat-${APP_VERSION}-${PLATFORM}-${ARCHITECTURE}-${RENDERER}.AppDir"
    RESOURCES="${RELEASE_DIR}/usr/share/ecat"

    # Очистка предыдущей сборки (опционально)
    # rm -rf "${BUILD_DIR}"

    # Сборка с разными рендерерами
    cmake \
        -DCMAKE_PREFIX_PATH="${QT_PATH}" \
        -DRENDERER_${RENDERER^^}=1 \
        -S ../src \
        -B "${BUILD_DIR}" \
        -G Ninja

    # Компиляция
    cmake --build "${BUILD_DIR}" --target all

    # Подготовка AppDir
    mkdir -p "${RELEASE_DIR}/usr/bin/"
    cp -r ./.linux/ecat3.AppDir/* "${RELEASE_DIR}"
    cp "${BUILD_DIR}/eCat3" "${RELEASE_DIR}/usr/bin/"

    mkdir -p "${RESOURCES}"
    cp -r ../deploy/* "${RESOURCES}"
    rm -f "${RESOURCES}/ecat.ini"
    cp ../deploy/.ecat.ini "${RESOURCES}/ecat.ini"

    # Создание AppImage
    cd release
    export VERSION="${APP_VERSION}-${PLATFORM}-${RENDERER}"
    "${LINUXDEPLOYQT}" "../${RELEASE_DIR}/usr/share/applications/ecat3.desktop" \
        -verbose=2 \
        -appimage \
        -no-translations \
        -qmake="${QT_PATH}/bin/qmake"
    cd ..
done