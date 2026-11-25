#!/usr/bin/env bash
#
# Build EDK2 UEFI firmware for the D-Robotics RDK X5 (Sunrise X5).
#
# This is an umbrella repository: the edk2 tree is a pinned git submodule.
#
# Usage:
#   ./build.sh [DEBUG|RELEASE|NOOPT]
#
# Environment:
#   X5_TOOLCHAIN_PREFIX  aarch64 GCC prefix (default: vendor 11.2 toolchain)
#   X5_EDK2_DIR          override the edk2 path (default: ./edk2)
#   JOBS                 parallel build jobs (default: nproc)
#
# SPDX-License-Identifier: BSD-2-Clause-Patent
#
set -euo pipefail

REPO=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
TARGET=${1:-DEBUG}
JOBS=${JOBS:-$(nproc)}
EDK2_DIR=${X5_EDK2_DIR:-${REPO}/edk2}

# 1. Fetch the edk2 submodule (and its submodules) on first use.
if [ ! -e "${EDK2_DIR}/BaseTools/Edk2ToolsBuild.py" ]; then
  echo "[x5] Initializing edk2 submodule (git submodule update --init --recursive)"
  git -C "${REPO}" submodule update --init --recursive edk2
fi

# 2. Build BaseTools if needed.
if [ ! -x "${EDK2_DIR}/BaseTools/Source/C/bin/GenFw" ]; then
  echo "[x5] Building BaseTools"
  make -C "${EDK2_DIR}/BaseTools" -j"${JOBS}"
fi

# 3. Build the X5 platform.
export X5_EDK2_DIR="${EDK2_DIR}"
exec "${REPO}/Platform/DRobotics/X5/Scripts/build_x5_uefi.sh" "${TARGET}"
