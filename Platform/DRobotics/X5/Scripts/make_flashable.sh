#!/usr/bin/env bash
#
# Turn the EDK2 BL33 FIP into flashable images for the X5 `uboot` NAND
# partition.
#
# Outputs (default: Build/X5-AArch64/FIP/flashable):
#   uboot.img            drop-in replacement for out/product/uboot.img
#   uboot_partition.img  FIP padded to the 2 MB `uboot` partition with 0xFF
#   nand_disk.img        (only if MINIBOOT_ALL and UBOOTENV_IMG are set)
#
# X5 NAND layout (x5-rdk-ubuntu-nand-gpt.json):
#   mbr 0x000000 256K | miniboot 0x040000 2304K | miniboot_bak1 0x280000 2304K
#   misc 0x4C0000 256K | uboot 0x500000 2M | ubootenv 0x700000 1536K
#
# SPDX-License-Identifier: BSD-2-Clause-Patent
#
set -euo pipefail

SCRIPT_DIR=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
EDK2_DIR=$(cd "${SCRIPT_DIR}/../../../.." && pwd)

FIP_DIR=${EDK2_DIR}/Build/X5-AArch64/FIP
FIP=${1:-${FIP_DIR}/edk2_bl33.img}
OUT=${2:-${FIP_DIR}/flashable}

UBOOT_PART_SIZE=$((2 * 1024 * 1024))
UBOOTENV_PART_SIZE=$((1536 * 1024))

if [ ! -f "${FIP}" ]; then
  echo "ERROR: FIP not found: ${FIP}" >&2
  echo "       run pack_bl33_fip.sh first" >&2
  exit 1
fi

mkdir -p "${OUT}"

FIP_SIZE=$(stat -c %s "${FIP}")
if [ "${FIP_SIZE}" -gt "${UBOOT_PART_SIZE}" ]; then
  echo "ERROR: FIP (${FIP_SIZE}) exceeds uboot partition (${UBOOT_PART_SIZE})" >&2
  exit 1
fi

# Drop-in replacement for the vendor out/product/uboot.img.
cp -f "${FIP}" "${OUT}/uboot.img"

# Partition-sized image padded with 0xFF (NAND erased state).
cp -f "${FIP}" "${OUT}/uboot_partition.img"
PAD=$((UBOOT_PART_SIZE - FIP_SIZE))
if [ "${PAD}" -gt 0 ]; then
  head -c "${PAD}" /dev/zero | tr '\000' '\377' >> "${OUT}/uboot_partition.img"
fi

echo "uboot.img            : ${OUT}/uboot.img (${FIP_SIZE} bytes)"
echo "uboot_partition.img  : ${OUT}/uboot_partition.img ($(stat -c %s "${OUT}/uboot_partition.img") bytes)"

# Optional: build the full NAND image if the vendor miniboot/ubootenv images
# are supplied (e.g. MINIBOOT_ALL=.../miniboot_all.img UBOOTENV_IMG=.../ubootenv.img).
if [ -n "${MINIBOOT_ALL:-}" ] && [ -n "${UBOOTENV_IMG:-}" ]; then
  mkdir -p "${OUT}"
  cp -f "${UBOOTENV_IMG}" "${OUT}/ubootenv_partition.img"
  ESIZE=$(stat -c %s "${UBOOTENV_IMG}")
  EPAD=$((UBOOTENV_PART_SIZE - ESIZE))
  if [ "${EPAD}" -lt 0 ]; then
    echo "ERROR: ubootenv image too large" >&2
    exit 1
  fi
  head -c "${EPAD}" /dev/zero | tr '\000' '\377' >> "${OUT}/ubootenv_partition.img"

  cat "${MINIBOOT_ALL}" "${OUT}/uboot_partition.img" "${OUT}/ubootenv_partition.img" \
    > "${OUT}/nand_disk.img"
  echo "nand_disk.img        : ${OUT}/nand_disk.img ($(stat -c %s "${OUT}/nand_disk.img") bytes)"
else
  echo
  echo "Full nand_disk.img not built (MINIBOOT_ALL / UBOOTENV_IMG not set)."
  echo "To assemble it:"
  echo "  MINIBOOT_ALL=<...>/miniboot_all.img UBOOTENV_IMG=<...>/ubootenv.img \\"
  echo "    $0"
fi
