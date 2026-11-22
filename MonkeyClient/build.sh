#!/usr/bin/env bash
# Build MonkeyClient for Meta Quest (aarch64) with the Android NDK.
# Usage: ANDROID_NDK=/path/to/ndk ./build.sh   (or set ANDROID_NDK_HOME / NDK_HOME)
set -euo pipefail
cd "$(dirname "$0")"

NDK="${ANDROID_NDK:-${ANDROID_NDK_HOME:-${NDK_HOME:-}}}"
if [[ -z "$NDK" ]]; then
    echo "ERROR: set ANDROID_NDK=/path/to/android-ndk (r25 or newer)" >&2
    exit 1
fi

cmake -B build-arm64 \
    -DCMAKE_TOOLCHAIN_FILE="$NDK/build/cmake/android.toolchain.cmake" \
    -DANDROID_ABI=arm64-v8a \
    -DANDROID_PLATFORM=android-25 \
    -DCMAKE_BUILD_TYPE=Release >/dev/null

cmake --build build-arm64 -j"$(nproc)"
echo
echo "Built: $(pwd)/build/libMonkeyClient.so"
