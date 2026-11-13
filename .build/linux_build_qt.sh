#!/bin/bash
#
# Builds the Qt kit of the Linux release (build-linux.sh) from the sources.
# The kit is shared, as the release always was: the AppImage carries the Qt
# libraries. The keys make Qt smaller, the same set as the static kits for
# Windows and macOS (docs/BUILD.md explains each), with these differences:
#
#   -no-feature-freetype is not here: on Linux FreeType with Fontconfig is the
#   font engine itself;
#   -no-icu is: Qt on Windows and macOS has no ICU either, and linuxdeployqt
#   would put its libraries (30 MB of them libicudata) into the AppImage;
#   -no-libjpeg -no-ico leave GIF the only image format of qtbase, as
#   qt_import_plugins does for a static kit: a shared Qt loads its plugins at
#   run time, and linuxdeployqt deploys every image format of the kit;
#   -xcb -fontconfig -opengl desktop stop configure when a package of
#   docs/BUILD.md is missing, instead of a kit without the window, the fonts
#   or the OpenGL renderer.
#
# The binaries of the Qt online installer are no help here: Qt 6.11 there
# needs glibc 2.34, and the release is built on Ubuntu 20.04 (glibc 2.31), so
# that it runs on older distributions too.
#
# Usage: bash linux_build_qt.sh [extract] [configure] [build]
# Without arguments it does all three, in this order.

set -euo pipefail

QT_VERSION=6.11.3
QT_SRC_TAR="${HOME}/Downloads/qt-everywhere-src-${QT_VERSION}.tar.xz"
WORK_BASE="${HOME}/dev"
QT_SRC_DIR="${WORK_BASE}/qt-everywhere-src-${QT_VERSION}"
BUILD_BASE="${WORK_BASE}/qt-build-linux"
# build-linux.sh takes Qt from here (QT_PATH)
INSTALL_BASE="${HOME}/Qt-${QT_VERSION}-shared"

# Link time code generation, as in the static kit for Windows. It has not been
# tried with GCC 9 and a shared Qt yet: if the kit does not build, or eCat3
# does not link with it, empty this and build the kit again
QT_LTCG="-ltcg"

extract_sources() {
    echo "Extracting Qt sources..."
    if [ ! -f "${QT_SRC_TAR}" ]; then
        echo "ERROR: no Qt sources at ${QT_SRC_TAR}" >&2
        exit 1
    fi
    rm -rf "${QT_SRC_DIR}"
    mkdir -p "${WORK_BASE}"
    tar xf "${QT_SRC_TAR}" -C "${WORK_BASE}"
}

configure_qt() {
    echo "Configuring Qt..."
    # From scratch: a configure over an earlier one keeps the features that
    # one found (CMakeCache.txt), and the keys below would not all apply
    rm -rf "${BUILD_BASE}"
    mkdir -p "${BUILD_BASE}"
    cd "${BUILD_BASE}"

    "${QT_SRC_DIR}/configure" \
        -release -nomake tests -nomake examples \
        -opensource -confirm-license \
        -optimize-size ${QT_LTCG} \
        -submodules qtbase,qtsvg,qttools,qttranslations \
        -skip qtdeclarative,qtshadertools,qtlanguageserver,qtimageformats,qtactiveqt \
        -no-icu -no-feature-timezone -no-feature-sha3-fast \
        -no-feature-mdiarea -no-feature-calendarwidget -no-feature-datetimeedit \
        -no-feature-colordialog -no-feature-textodfwriter \
        -no-feature-hijricalendar -no-feature-jalalicalendar \
        -no-feature-designer -no-feature-assistant \
        -no-libjpeg -no-ico \
        -xcb -fontconfig -opengl desktop \
        -prefix "${INSTALL_BASE}"
}

build_qt() {
    echo "Building Qt..."
    cd "${BUILD_BASE}"
    cmake --build . --parallel
    # Into an empty prefix: linuxdeployqt takes the plugins from the kit, and
    # those of an earlier one would end up in the AppImage
    rm -rf "${INSTALL_BASE}"
    cmake --install .
    echo "Qt ${QT_VERSION} is in ${INSTALL_BASE}"
}

STEPS=("$@")
if [ ${#STEPS[@]} -eq 0 ]; then
    STEPS=(extract configure build)
fi

for STEP in "${STEPS[@]}"; do
    case "${STEP}" in
        extract)   extract_sources ;;
        configure) configure_qt ;;
        build)     build_qt ;;
        *)
            echo "ERROR: unknown step '${STEP}' (extract, configure, build)" >&2
            exit 1
            ;;
    esac
done
