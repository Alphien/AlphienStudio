#!/usr/bin/env bash
set -e

# Script to build AlphienStudio from source
WORK_DIR="$PWD"
BUILD_DIR="$WORK_DIR/build"

# Patching AlphienStudio
patch -Np1 -i "$WORK_DIR/patch/no-sso.patch"

# Create build folder
mkdir -pv "$BUILD_DIR"

# Change current working directory to build folder
cd "$BUILD_DIR/../dependencies/linux/"

# Install dependencies for building
echo "Installing dependencies"
./install-dependencies-debian

cd "$BUILD_DIR"

# Prepare alphien studio for server release
cmake .. -DRSTUDIO_TARGET=Server -DCMAKE_BUILD_TYPE=Release | tee AlphienStudio-config.log

# Build and install
make -j 4 | tee AlphienStudio-make.log
make install |tee AlphienStudio-install.log

echo "Rstudio installed"