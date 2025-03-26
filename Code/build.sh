#!/usr/bin/env bash

CUR_FOLDER="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PARENT_DIR="$(cd "$CUR_FOLDER/.." && pwd)"
TMP_FOLDER="$PARENT_DIR/tmp"
mkdir -p "$TMP_FOLDER"
echo "Tmp folder: $TMP_FOLDER"

# Check for fbuild in the current directory
if [ ! -f "$CUR_FOLDER/fbuild" ]; then
  echo "Error: fbuild not found in the current directory: $CUR_FOLDER"
  exit 1
fi

# Run builds
"$CUR_FOLDER/fbuild" All-x64Linux-Release "$@"
if [ $? -ne 0 ]; then
  echo "Build Failed !!!"
  exit 1
fi

"$CUR_FOLDER/fbuild" All-x64Linux-Debug "$@"
if [ $? -ne 0 ]; then
  echo "Build Failed !!!"
  exit 1
fi

# Extract version
VERSION_FILE="$CUR_FOLDER/Tools/FBuild/FBuildCore/FBuildVersion.h"
VERSION="$(grep '#define FBUILD_VERSION_STRING' "$VERSION_FILE" | awk '{print $3}' | tr -d '\"\r')"
echo "Version: $VERSION"

# Copy executables to tmp folders
OUTPUTS=("x64Linux-Release" "x64Linux-Debug")
for out in "${OUTPUTS[@]}"; do
  BIN_DIR="$TMP_FOLDER/$out/bin"
  echo "Creating directory: $BIN_DIR"
  mkdir -p "$BIN_DIR"
  cp -r "$TMP_FOLDER/$out/Tools/FBuild/FBuild/FBuild" "$BIN_DIR" 2>/dev/null
  cp -r "$TMP_FOLDER/$out/Tools/FBuild/FBuildCoordinator/FBuildCoordinator" "$BIN_DIR" 2>/dev/null
  cp -r "$TMP_FOLDER/$out/Tools/FBuild/FBuildWorker/FBuildWorker" "$BIN_DIR" 2>/dev/null
done

# Create final output folder
BIN_FOLDER="$PARENT_DIR/FASTBuild-Linux-x64-$VERSION"
mkdir -p "$BIN_FOLDER"
cp "$TMP_FOLDER/x64Linux-Release/bin/FBuild" "$BIN_FOLDER" 2>/dev/null
cp "$TMP_FOLDER/x64Linux-Release/bin/FBuildCoordinator" "$BIN_FOLDER" 2>/dev/null
cp "$TMP_FOLDER/x64Linux-Release/bin/FBuildWorker" "$BIN_FOLDER" 2>/dev/null

echo
echo "Build Succeed !!!"
echo "Build ready in folder: $BIN_FOLDER"
