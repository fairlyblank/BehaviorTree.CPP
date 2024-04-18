#!/bin/sh

source ./env-linux-x64.sh

TARGET_PLATFORM_DEFAULT=x64-mingw-static

if [ ! -z ${TARGET_PLATFORM} ]; then
    TARGET_TRIPLET=${TARGET_PLATFORM}
else
    TARGET_TRIPLET=${TARGET_PLATFORM_DEFAULT}
fi

HOST_TRIPLET=x64-mingw-static

mkdir -p ./.build-linux-x64

#rm -R ./.build-win/CMakeFiles 2>/dev/null
#rm ./.build-win/cmake_install.cmake 2>/dev/null
#rm ./.build-win/CMakeCache.txt 2>/dev/null
#rm ./.build-win/Makefile 2>/dev/null

echo "TARGET_TRIPLET:${TARGET_TRIPLET}"
echo "HOST_TRIPLET:${HOST_TRIPLET}"
echo "REGISTRIES_ROOT:${REGISTRIES_ROOT}"
echo "VCPKG_ROOT:${VCPKG_ROOT}"

cmake -B ./.build-linux-x64 -S . -G"MinGW Makefiles" \
      "-DVCPKG_TARGET_TRIPLET:STRING=${TARGET_TRIPLET}" \
      "-DVCPKG_HOST_TRIPLET:STRING=${HOST_TRIPLET}" \
      "-DVCPKG_OVERLAY_TRIPLETS:STRING=${REGISTRIES_ROOT}/triplets" \
      "-DVCPKG_OVERLAY_PORTS:STRING=${REGISTRIES_ROOT}/ports-overlay" \
      "-DCMAKE_TOOLCHAIN_FILE=${VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake"
