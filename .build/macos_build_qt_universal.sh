#!/bin/bash

set -e

QT_VERSION=6.11.3
QT_SRC_TAR=~/Downloads/qt-everywhere-src-$QT_VERSION.tar.xz
QT_SRC_DIR=/tmp/qt-everywhere-src-$QT_VERSION
INSTALL_BASE=~/Qt-$QT_VERSION-static-universal
BUILD_BASE=~/dev/qt-build-universal
SYSTEM_PREFIX=/usr/local/Qt-$QT_VERSION-static-universal

# Link time code generation, as in the static kit for Windows. It has not been
# tried on macOS yet: if eCat3 does not link with the kit, empty this and build
# the kit again
QT_LTCG="-ltcg"

detect_shell_config() {
    if [[ -n "$ZSH_VERSION" ]]; then
        echo "$HOME/.zshrc"
    elif [[ -n "$BASH_VERSION" ]]; then
        echo "$HOME/.bash_profile"
    else
        echo "$HOME/.profile"
    fi
}

SHELL_CONFIG=$(detect_shell_config)

extract_sources() {
    echo "Extracting Qt sources..."
    rm -rf "$QT_SRC_DIR"
    mkdir -p "$QT_SRC_DIR"
    tar xf "$QT_SRC_TAR" -C /tmp
}

# The keys from -optimize-size to -no-feature-assistant make Qt in the program
# smaller and the build shorter: the same set as the static kit for Windows,
# each of them is explained in docs/BUILD.md. All but -no-feature-mdiarea: the
# macos style plugin uses QMdiSubWindow without a check (qmacstyle_mac.mm of
# Qt 6.11.3), and Qt does not build without it
configure_universal() {
    echo "Configuring universal Qt..."
    # From scratch: a configure over an earlier one keeps the features that
    # one found (CMakeCache.txt), and the keys below would not all apply
    rm -rf "$BUILD_BASE"
    mkdir -p "$BUILD_BASE"
    cd "$BUILD_BASE"

    $QT_SRC_DIR/configure \
        -static -release -nomake tests -nomake examples \
        -opensource -confirm-license \
        -optimize-size $QT_LTCG \
        -submodules qtbase,qtsvg,qttools,qttranslations \
        -skip qtdeclarative,qtshadertools,qtlanguageserver,qtimageformats,qtactiveqt \
        -no-feature-freetype -no-feature-timezone -no-feature-sha3-fast \
        -no-feature-calendarwidget -no-feature-datetimeedit \
        -no-feature-colordialog -no-feature-textodfwriter \
        -no-feature-hijricalendar -no-feature-jalalicalendar \
        -no-feature-designer -no-feature-assistant \
        -platform macx-clang \
        -no-rpath \
        -prefix $INSTALL_BASE \
        -- \
        -DCMAKE_OSX_ARCHITECTURES="arm64;x86_64"
}

build_universal() {
    echo "Building universal Qt..."
    cd "$BUILD_BASE"
    cmake --build . --parallel
    # Into an empty prefix: modules and plugins of an earlier kit would stay
    # there, and a static Qt links every plugin it finds into the program
    rm -rf "$INSTALL_BASE"
    cmake --install .
}


install_to_system() {
    echo "Installing to $SYSTEM_PREFIX..."
    sudo rm -rf "$SYSTEM_PREFIX"
    sudo mkdir -p "$SYSTEM_PREFIX"
    sudo cp -R "$INSTALL_BASE/"* "$SYSTEM_PREFIX/"
}

update_env() {
    echo "Updating $SHELL_CONFIG..."
    ADD_PATH="export PATH=\"$SYSTEM_PREFIX/bin:\$PATH\""
    ADD_CMAKE="export CMAKE_PREFIX_PATH=\"$SYSTEM_PREFIX:\$CMAKE_PREFIX_PATH\""

    grep -qxF "$ADD_PATH" "$SHELL_CONFIG" || echo "$ADD_PATH" >> "$SHELL_CONFIG"
    grep -qxF "$ADD_CMAKE" "$SHELL_CONFIG" || echo "$ADD_CMAKE" >> "$SHELL_CONFIG"

    echo "Environment updated -- restart your terminal or run:"
    echo "   source $SHELL_CONFIG"
}

menu() {
    while true; do
        echo "\n==== Qt Build Menu ===="
        echo "1) Extract sources"
        echo "2) Configure universal (arm64 + x86_64)"
        echo "3) Build universal (arm64 + x86_64)"
        echo "4) Install to system path"
        echo "5) Update environment variables"
        echo "6) Run everything sequentially"
        echo "7) Exit"
        echo "======================="

        read -p "Choose an operation [1-7]: " REPLY

        case $REPLY in
            1) extract_sources ;;
            2) configure_universal ;;
            3) build_universal ;;
            4) install_to_system ;;
            5) update_env ;;
            6)
                extract_sources
                configure_universal
                build_universal
                install_to_system
                update_env
                ;;
            7) echo "Exiting..."; break ;;
            *) echo "Invalid choice" ;;
        esac
    done
}

menu
