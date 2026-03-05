#!/bin/bash
# Глушим ошибки Perl
export LC_ALL=C
export LANG=C

ARGS=$(echo "$*" | tr 'A-Z' 'a-z')
command -v cmake >/dev/null || exit 1

if command -v ninja >/dev/null; then
    GEN="Ninja"; BUILD="ninja"
elif command -v make >/dev/null; then
    GEN="Unix Makefiles"; BUILD="make -j$(sysctl -n hw.ncpu)"
else exit 1; fi

TYPE="Debug"; [[ "$ARGS" == *"release"* ]] && TYPE="Release"
DIR="_$(echo $TYPE | tr 'A-Z' 'a-z')"

[[ "$ARGS" == *"clean"* ]] && rm -rf "$DIR"
mkdir -p "$DIR" && cd "$DIR" || exit 1

if [[ ! -f "CMakeCache.txt" ]]; then
    cmake -G "$GEN" -DCMAKE_BUILD_TYPE="$TYPE" \
        -DCMAKE_PREFIX_PATH="/opt/homebrew" \
        -DCMAKE_INSTALL_RPATH="/opt/homebrew/lib" \
        -DCMAKE_BUILD_RPATH="/opt/homebrew/lib" \
        -DCMAKE_INSTALL_PREFIX="${DIR}/install" ..
fi

$BUILD

if [[ "$ARGS" == *"dmg"* ]]; then
    # Execute install step to ensure the app bundle is created
    echo "Running install target..."
    $BUILD install
    if [ $? -ne 0 ]; then
        echo "Error: Failed to run install target."
        exit 1
    fi

    # Хирургический патч: заставляем BundleUtilities искать в Homebrew
    FIXUP="packaging/osx/FixupBundle.cmake"
    if [[ -f "$FIXUP" ]]; then
        sed -i '' 's|fixup_bundle(|set(DIRS "/opt/homebrew/lib" ${DIRS})\nfixup_bundle(|g' "$FIXUP"
    fi
    
    # Ad-hoc code signing for development purposes
    APP_BUNDLE_PATH="${DIR}/install/far2l.app"
    echo "Signing the application bundle: ${APP_BUNDLE_PATH}"
    codesign --force --deep --sign - "${APP_BUNDLE_PATH}"
    if [ $? -ne 0 ]; then
        echo "Error: Failed to ad-hoc sign the application bundle."
        exit 1
    fi
    
    cpack -G DragNDrop
fi
