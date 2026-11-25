#!/usr/bin/env bash
#
# Build the RDK X5 (Sunrise X5) EDK2 UEFI firmware.
#
# Usage:
#   ./build_x5_uefi.sh [DEBUG|RELEASE|NOOPT]
#
# Environment overrides:
#   X5_TOOLCHAIN_PREFIX  aarch64 GCC prefix (default: vendor 11.2 toolchain)
#
# The BL33 payload for the vendor FIP is produced at:
#   Build/X5-AArch64/<TARGET>_GCC5/FV/X5_EFI.fd
#
# SPDX-License-Identifier: BSD-2-Clause-Patent
#
set -euo pipefail

SCRIPT_DIR=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
# Root that contains Platform/ (the edk2 workspace when built in-tree, or the
# edk-rdkx5 repository root when used as an out-of-tree package).
PKG_ROOT=$(cd "${SCRIPT_DIR}/../../../.." && pwd)
# edk2 workspace: defaults to PKG_ROOT for in-tree builds; set X5_EDK2_DIR when
# the package lives in a separate repository.
EDK2_DIR=${X5_EDK2_DIR:-${PKG_ROOT}}

TARGET=${1:-DEBUG}
TOOLCHAIN_PREFIX=${X5_TOOLCHAIN_PREFIX:-/opt/gcc-arm-11.2-2022.02-x86_64-aarch64-none-linux-gnu/bin/aarch64-none-linux-gnu-}

export WORKSPACE="${EDK2_DIR}"
export EDK_TOOLS_PATH="${EDK2_DIR}/BaseTools"
export PYTHON_COMMAND=${PYTHON_COMMAND:-python3}
export GCC5_AARCH64_PREFIX="${TOOLCHAIN_PREFIX}"
export PATH="${EDK_TOOLS_PATH}/BinWrappers/PosixLike:${PATH}"
# Search both the edk2 workspace and this package repository for packages.
export PACKAGES_PATH="${PKG_ROOT}:${EDK2_DIR}${PACKAGES_PATH:+:${PACKAGES_PATH}}"

if [ ! -x "${TOOLCHAIN_PREFIX}gcc" ]; then
  echo "ERROR: aarch64 GCC not found: ${TOOLCHAIN_PREFIX}gcc" >&2
  echo "       set X5_TOOLCHAIN_PREFIX to a valid cross toolchain prefix" >&2
  exit 1
fi

cd "${EDK2_DIR}"

build \
  -a AARCH64 \
  -t GCC5 \
  -p Platform/DRobotics/X5/X5.dsc \
  -b "${TARGET}" \
  -n "$(nproc)"

FD="${EDK2_DIR}/Build/X5-AArch64/${TARGET}_GCC5/FV/X5_EFI.fd"
echo
echo "EDK2 X5 UEFI BL33 payload: ${FD}"
ls -l "${FD}"
