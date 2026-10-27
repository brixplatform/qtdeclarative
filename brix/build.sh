#!/usr/bin/env bash
# SPDX-License-Identifier: BSD-3-Clause
set -euo pipefail
repo=$(cd "$(dirname "$0")/.." && pwd)
build=${1:-"$repo/build-brix"}
mkdir -p "$build"
build=$(realpath "$build")
qtpaths=${QTPATHS:-/usr/lib/qt6/bin/qtpaths}
qt_libs=$("$qtpaths" --query QT_INSTALL_LIBS)
extra=()
if [[ ! -f "$qt_libs/cmake/Qt6LanguageServerPrivate/Qt6LanguageServerPrivateConfig.cmake" ]]; then
    # Some SDKs package the qmlls executable but omit the protocol SDK.
    dependency="$build/_deps/qtlanguageserver"
    prefix="$build/_deps/sdk"
    revision=c536390c64fbd21ff2981280034be71d714727f8
    if [[ ! -d "$dependency/.git" ]]; then
        git clone --depth 1 --branch v6.12.0 https://github.com/qt/qtlanguageserver.git "$dependency"
    fi
    [[ $(git -C "$dependency" rev-parse HEAD) == "$revision" ]] || {
        echo 'Unexpected qtlanguageserver revision; refusing an unpinned dependency.' >&2
        exit 1
    }
    cmake -S "$dependency" -B "$build/_deps/protocol-build" -GNinja \
        -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX="$prefix" \
        -DQT_BUILD_TESTS=OFF -DQT_BUILD_EXAMPLES=OFF
    cmake --build "$build/_deps/protocol-build" --parallel 4
    cmake --install "$build/_deps/protocol-build"
    extra=(-DQT_ADDITIONAL_PACKAGES_PREFIX_PATH="$prefix")
fi
cmake -S "$repo/brix" -B "$build" -GNinja -DCMAKE_BUILD_TYPE=Release "${extra[@]}"
cmake --build "$build" --parallel 3
ctest --test-dir "$build" --output-on-failure
