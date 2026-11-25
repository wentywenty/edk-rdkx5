#!/usr/bin/env bash
#
# Package the EDK2 X5 UEFI firmware as the BL33 (`nt-fw`) image that the X5
# vendor boot chain (TF-A BL2 -> BL31 -> BL33) expects.
#
# This mirrors x5-bootloader/build/mk_uboot.sh (gen_bl2_cfg + uboot_cert +
# pack_uboot), replacing u-boot.bin with the EDK2 FD.
#
# Usage:
#   ./pack_bl33_fip.sh [path/to/X5_EFI.fd]
#
# Produces (by default) Build/X5-AArch64/FIP/edk2_bl33.img, which can be
# flashed to the `uboot` partition in place of uboot.img.
#
# Environment overrides:
#   RDK_BOOTLOADER   path to x5-bootloader (vendor packaging tree)
#   X5_BOARD_CONF    path to .../device/rdk/x5/board_cfg/soc
#   X5_FIP_OUT       output directory
#
# NOTE: the vendor `uboot` NAND partition is 2 MB. A 2 MB FD plus the FIP
#       header and certificates can exceed that; if it does, enlarge the
#       partition or trim the FD (see the size check below).
#
# SPDX-License-Identifier: BSD-2-Clause-Patent
#
set -euo pipefail

SCRIPT_DIR=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
PKG_ROOT=$(cd "${SCRIPT_DIR}/../../../.." && pwd)
EDK2_DIR=${X5_EDK2_DIR:-${PKG_ROOT}}
EDK_ROOT=$(cd "${EDK2_DIR}/.." && pwd)

RDK_BOOTLOADER=${RDK_BOOTLOADER:-${EDK_ROOT}/rdk/x5-bootloader}
X5_BOARD_CONF=${X5_BOARD_CONF:-${RDK_BOOTLOADER}/device/rdk/x5/board_cfg/soc}
X5_FIP_OUT=${X5_FIP_OUT:-${EDK2_DIR}/Build/X5-AArch64/FIP}

FD_IN=${1:-${EDK2_DIR}/Build/X5-AArch64/DEBUG_GCC5/FV/X5_EFI.fd}

FIPTOOL=${RDK_BOOTLOADER}/build/tools/fiptool
CERT_TOOL=${RDK_BOOTLOADER}/build/tools/cert_create
BL2CFG_TOOL=${RDK_BOOTLOADER}/build/tools/bl2_cfg.py
BL2_ROT_KEY=${X5_BOARD_CONF}/bl2_cfg/bl2_rot_prikey.pem
BL2_CFG_JSON=${X5_BOARD_CONF}/bl2_cfg/bl2_cfg.json
USER_ROOT_KEY=${X5_BOARD_CONF}/bl2_cfg/user_root.key

for f in "${FD_IN}" "${FIPTOOL}" "${CERT_TOOL}" "${BL2CFG_TOOL}" \
         "${BL2_ROT_KEY}" "${BL2_CFG_JSON}" "${USER_ROOT_KEY}"; do
  if [ ! -e "${f}" ]; then
    echo "ERROR: required file not found: ${f}" >&2
    exit 1
  fi
done

mkdir -p "${X5_FIP_OUT}"

NT_FW=${X5_FIP_OUT}/nt-fw.bin
cp -f "${FD_IN}" "${NT_FW}"

# 1. Generate bl2_cfg.bin (magic "HBBL2CFG") consumed by BL2.
python3 "${BL2CFG_TOOL}" \
  "${BL2_CFG_JSON}" "${BL2_ROT_KEY}" "${USER_ROOT_KEY}" \
  "${X5_FIP_OUT}/bl2_cfg.bin"

# 2. Create the TBBR certificate chain for nt-fw.
"${CERT_TOOL}" \
  -n \
  --bl2-rot-key   "${BL2_ROT_KEY}" \
  --tfw-nvctr     0 \
  --ntfw-nvctr    0 \
  --key-alg       rsa \
  --key-size      4096 \
  --hash-alg      sha256 \
  --trusted-key-cert "${X5_FIP_OUT}/trusted_key.crt" \
  --nt-fw-key-cert   "${X5_FIP_OUT}/nt_fw_key.crt" \
  --nt-fw-cert       "${X5_FIP_OUT}/nt_fw_content.crt" \
  --nt-fw            "${NT_FW}" \
  --hb-bl2-cfg       "${X5_FIP_OUT}/bl2_cfg.bin"

# 3. Build the FIP that BL2 loads from the `uboot` partition.
"${FIPTOOL}" create \
  --trusted-key-cert "${X5_FIP_OUT}/trusted_key.crt" \
  --nt-fw-cert       "${X5_FIP_OUT}/nt_fw_content.crt" \
  --nt-fw-key-cert   "${X5_FIP_OUT}/nt_fw_key.crt" \
  --nt-fw            "${NT_FW}" \
  --hb-bl2-cfg       "${X5_FIP_OUT}/bl2_cfg.bin" \
  "${X5_FIP_OUT}/edk2_bl33.img"

echo
"${FIPTOOL}" info "${X5_FIP_OUT}/edk2_bl33.img" || true

SIZE=$(stat -c %s "${X5_FIP_OUT}/edk2_bl33.img")
PART_SIZE=$((2 * 1024 * 1024))
echo
echo "FIP image: ${X5_FIP_OUT}/edk2_bl33.img (${SIZE} bytes, partition ${PART_SIZE} bytes)"
if [ "${SIZE}" -gt "${PART_SIZE}" ]; then
  echo "WARNING: FIP exceeds the 2 MB uboot partition; resize the partition or trim the FD." >&2
fi
