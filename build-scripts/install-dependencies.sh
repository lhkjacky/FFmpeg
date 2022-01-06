#!/bin/bash

set -e

pacman -S --noconfirm mingw-w64-x86_64-nasm mingw-w64-x86_64-gcc mingw-w64-x86_64-SDL2 make pkg-config diffutils yasm git

cd ${DEPENDENCIES_DIR}
mkdir -p nv_sdk
git clone https://git.videolan.org/git/ffmpeg/nv-codec-headers.git
cd nv-codec-headers
make install
cd ..
cp -r "${CUDA_PATH}/include/." "${DEPENDENCIES_DIR}/nv_sdk/"
cp -r "${CUDA_PATH}/lib/x64/." "${DEPENDENCIES_DIR}/nv_sdk/"

git clone https://github.com/GPUOpen-LibrariesAndSDKs/AMF.git AMF-git
mkdir -p include/AMF
cp -r AMF-git/amf/public/include/. include/AMF/

pacman -S --noconfirm mingw-w64-x86_64-libmfx
echo "Please make sure that you have manually built and placed libmfx.lib in ${DPENDENCIES_DIR}/msdk-lib"

echo "When ready:"
echo './configure --toolchain=msvc --enable-shared --enable-nvenc --enable-nvdec --disable-vulkan --enable-amf --enable-libmfx --extra-cflags="-I${DEPENDENCIES_DIR}/nv_sdk -I${DEPENDENCIES_DIR}/include -I/mingw64/include" --extra-ldflags="-libpath:${DEPENDENCIES_DIR}/nv_sdk -libpath:${DEPENDENCIES_DIR}/msdk-lib"'
