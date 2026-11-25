#!/usr/bin/env bash
#
# Build EDK2 UEFI firmware for the D-Robotics RDK X5 (Sunrise X5).
#
# Usage:
#   ./build.sh [DEBUG|RELEASE|NOOPT]
#
# Environment:
#   X5_EDK2_DIR          path to an existing edk2 checkout (default: ./edk2)
#   X5_TOOLCHAIN_PREFIX  aarch64 GCC prefix (default: vendor 11.2 toolchain)
#   EDK2_TAG             edk2 tag to clone when ./edk2 is absent
#
# SPDX-License-Identifier: BSD-2-Clause-Patent
#
set -euo pipefail

REPO=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
TARGET=${1:-DEBUG}
EDK2_TAG=${EDK2_TAG:-edk2-stable202511}
EDK2_DIR=${X5_EDK2_DIR:-${REPO}/edk2}

if [ ! -d "${EDK2_DIR}/.git" ]; then
  echo "[x5] Cloning edk2 ${EDK2_TAG} into ${EDK2_DIR}"
  git clone --depth 1 --branch "${EDK2_TAG}" https://github.com/tianocore/edk2.git "${EDK2_DIR}"
  git -C "${EDK2_DIR}" submodule update --init --recursive --depth 1
fi

if [ ! -x "${EDK2_DIR}/BaseTools/Source/C/bin/GenFw" ]; then
  echo "[x5] Building BaseTools"
  make -C "${EDK2_DIR}/BaseTools" -j"$(nproc)"
fi

export X5_EDK2_DIR="${EDK2_DIR}"
exec "${REPO}/Platform/DRobotics/X5/Scripts/build_x5_uefi.sh" "${TARGET}"
