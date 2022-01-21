#!/bin/bash

if [[ -z "$CUDA_PATH" ]]; then
	echo "CUDA_PATH is not set. Aborting."
	return 1
elif [[ -z "$VCINSTALLDIR" ]]; then
	return "Couldn't find Visual Studio install location. Aborting."
	exit 1
elif [[ -z "$WindowsSdkVerBinPath" ]]; then
	echo "WindowsSdkVerBinPath is not set. Aborting."
	return 1
fi

export PATH="${VCINSTALLDIR}/Tools/MSVC/${VCToolsVersion}/bin/Hostx64/x64/":$PATH
export PATH="${CUDA_PATH}/bin/":$PATH
export PATH="${WindowsSdkVerBinPath}/x64/":$PATH
export PKG_CONFIG_PATH="/c/msys64/usr/local/lib/pkgconfig":$PKG_CONFIG_PATH

