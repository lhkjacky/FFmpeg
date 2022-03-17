#!/bin/bash

VERSION=$1

set -e

USER="$2"
CHANNEL="$3"

TOPAZ_CONAN=$4
OPENH264_ARM=$5
OPENH264_X86=$6

cd ../..
DO_CONAN_EXPORT=1 CONAN_PACKAGES=${TOPAZ_CONAN} PKG_VERSION=${VERSION} bash build-scripts/mac/build_mac.sh 1 ./builds-arm ./builds-x86 ./builds-univ '' '' ${OPENH264_ARM} ${OPENH264_X86}
cp build-scripts/deploy_conanfile.py ${TOPAZ_CONAN}/prebuilt/ffmpeg/${VERSION}/conanfile.py


cd ${TOPAZ_CONAN}
bash ./run_publish_prebuilt.sh --conan-channel ${CHANNEL} --conan-user ${USER} --package-name ffmpeg --package-version ${VERSION}
