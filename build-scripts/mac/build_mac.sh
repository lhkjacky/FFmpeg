#!/bin/bash

#OLD_PKG_CONFIG_PATH=$PKG_CONFIG_PATH

if [[ "$USE_DEFAULT_PKG_CONFIG" -ne 1 ]] && [ -z "$OPENH264_X86_PKG_CONFIG_PATH" -o -z "$OPENH264_ARM_PKG_CONFIG_PATH" ]; then
	echo 'Please ensure $OPENH264_X86_PKG_CONFIG_PATH and $OPENH264_ARM_PKG_CONFIG_PATH are set, or run this script with USE_DEFAULT_PKG_CONFIG=1'
	exit 1
fi	

if [ -z "$2" -o -z "$3" -o \( "$1" = "1" -a \( -z "$4" -o -z "$5" \) \) ]; then
	echo "Usage: ./build_mac.sh [veai enabled? (0 or 1)] [arm output path] [x86 output path] [conan arm] [conan x86] (extra ldflags)"
	exit 1
fi;

CONAN_ARM=$4
CONAN_X86=$5

FLAGS=(--enable-libvpx --enable-libopenh264)
XFLAGS=(--arch=x86_64 --extra-cflags="-arch x86_64" --extra-ldflags="-arch x86_64")
if [[ "$1" -eq 1 ]]; then
	FLAGS=(--extra-cflags="-I${CONAN_ARM}/include/veai $6" --extra-ldflags="-L${CONAN_ARM}/lib $7" --enable-veai ${FLAGS[@]})
	XFLAGS=(--extra-cflags="-arch x86_64 -I${CONAN_ARM}/include/veai $2" --extra-ldflags="-arch x86_64 -L${CONAN_ARM}/lib $3")
fi

set -e
#export PKG_CONFIG_PATH=$OPENH264_ARM_PKG_CONFIG_PATH:$OLD_PKG_CONFIG_PATH
make clean
echo ./configure --prefix=$2 --disable-asm "${FLAGS[@]}" --env=PKG_CONFIG_PATH="$OPENH264_ARM_PKG_CONFIG_PATH":$PKG_CONFIG_PATH
./configure --prefix=$2 --disable-asm "${FLAGS[@]}" --env=PKG_CONFIG_PATH="$OPENH264_ARM_PKG_CONFIG_PATH":$PKG_CONFIG_PATH
make clean
make -j8 install

#export PKG_CONFIG_PATH=$OPENH264_X86_PKG_CONFIG_PATH:$OLD_PKG_CONFIG_PATH
make clean
echo ./configure --prefix=$3 "${XFLAGS[@]}" --env=PKG_CONFIG_PATH="$OPENH264_X86_PKG_CONFIG_PATH":$PKG_CONFIG_PATH
./configure --prefix=$3 "${XFLAGS[@]}" --env=PKG_CONFIG_PATH="$OPENH264_X86_PKG_CONFIG_PATH":$PKG_CONFIG_PATH
make clean
make -j8 install

#export PKG_CONFIG_PATH=$OLD_PKG_CONFIG_PATH
