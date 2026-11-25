/** @file

  X5 (RDK X5 / Sunrise X5) ArmVirtMemInfoLib implementation.

  The X5 boot chain (BL2 -> BL31 -> BL33) does not hand a device tree to the
  BL33 payload, so the memory map is described statically here instead of being
  parsed from a DT, as the generic QEMU variant does.

  Physical layout on X5 (see x5-uboot/include/configs/x5.h):

    0x0_0000_0000  .. 0x0_2FFF_FFFF  low memory / boot ROM hooks
    0x0_3000_0000  .. 0x0_3FFF_FFFF  SoC peripherals (GIC, UART, storage, ...)
    0x0_8000_0000  .. 0x0_83FF_FFFF  secure DRAM, owned by BL31 / OP-TEE
    0x0_8400_0000  .. 0x0_FFFF_FFFF  non-secure DRAM (1984 MB on 2 GB part)
    0x1_0000_0000  ..                high DRAM (capacity dependent)

  The EDK2 image itself is loaded as the BL33 payload at 0x0_8800_0000, inside
  the non-secure DRAM window, so the DRAM region is split around the FD.

  SPDX-License-Identifier: BSD-2-Clause-Patent

**/

#include <Uefi.h>
#include <Pi/PiMultiPhase.h>
#include <Library/ArmLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/DebugLib.h>
#include <Library/HobLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/PcdLib.h>

#define X5_MAX_MEMORY_MAP_DESCRIPTORS  5

// Entire SoC peripheral window (GIC, UART, RTC, watchdog, eMMC/SD, QSPI).
#define X5_PERIPH_BASE  0x30000000
#define X5_PERIPH_SIZE  0x10000000

VOID
ArmVirtGetMemoryMap (
  OUT ARM_MEMORY_REGION_DESCRIPTOR  **VirtualMemoryMap
  )
{
  ARM_MEMORY_REGION_DESCRIPTOR  *VirtualMemoryTable;
  UINT64                        SystemMemoryBase;
  UINT64                        SystemMemorySize;
  UINT64                        SystemMemoryEnd;
  UINT64                        FdBase;
  UINT64                        FdSize;
  UINT64                        FdEnd;
  UINTN                         Index;

  ASSERT (VirtualMemoryMap != NULL);

  SystemMemoryBase = PcdGet64 (PcdSystemMemoryBase);
  SystemMemorySize = PcdGet64 (PcdSystemMemorySize);
  SystemMemoryEnd  = SystemMemoryBase + SystemMemorySize;
  FdBase           = PcdGet64 (PcdFdBaseAddress);
  FdSize           = PcdGet32 (PcdFdSize);
  FdEnd            = FdBase + FdSize;

  VirtualMemoryTable = AllocatePool (
                         sizeof (ARM_MEMORY_REGION_DESCRIPTOR) *
                         X5_MAX_MEMORY_MAP_DESCRIPTORS
                         );
  if (VirtualMemoryTable == NULL) {
    DEBUG ((DEBUG_ERROR, "%a: Failed to allocate memory map\n", __func__));
    return;
  }

  ZeroMem (VirtualMemoryTable,
           sizeof (ARM_MEMORY_REGION_DESCRIPTOR) * X5_MAX_MEMORY_MAP_DESCRIPTORS);

  Index = 0;

  // Non-secure DRAM below the firmware FD.
  if (FdBase > SystemMemoryBase) {
    VirtualMemoryTable[Index].PhysicalBase = SystemMemoryBase;
    VirtualMemoryTable[Index].VirtualBase  = SystemMemoryBase;
    VirtualMemoryTable[Index].Length       = FdBase - SystemMemoryBase;
    VirtualMemoryTable[Index].Attributes   = ARM_MEMORY_REGION_ATTRIBUTE_WRITE_BACK;
    Index++;
  }

  // The firmware FD itself (must be executable cached memory).
  VirtualMemoryTable[Index].PhysicalBase = FdBase;
  VirtualMemoryTable[Index].VirtualBase  = FdBase;
  VirtualMemoryTable[Index].Length       = FdSize;
  VirtualMemoryTable[Index].Attributes   = ARM_MEMORY_REGION_ATTRIBUTE_WRITE_BACK;
  Index++;

  // Non-secure DRAM above the firmware FD.
  if (SystemMemoryEnd > FdEnd) {
    VirtualMemoryTable[Index].PhysicalBase = FdEnd;
    VirtualMemoryTable[Index].VirtualBase  = FdEnd;
    VirtualMemoryTable[Index].Length       = SystemMemoryEnd - FdEnd;
    VirtualMemoryTable[Index].Attributes   = ARM_MEMORY_REGION_ATTRIBUTE_WRITE_BACK;
    Index++;
  }

  // SoC peripherals.
  VirtualMemoryTable[Index].PhysicalBase = X5_PERIPH_BASE;
  VirtualMemoryTable[Index].VirtualBase  = X5_PERIPH_BASE;
  VirtualMemoryTable[Index].Length       = X5_PERIPH_SIZE;
  VirtualMemoryTable[Index].Attributes   = ARM_MEMORY_REGION_ATTRIBUTE_DEVICE;
  Index++;

  DEBUG ((
    DEBUG_INFO,
    "%a: DRAM 0x%lx-0x%lx (FD 0x%lx-0x%lx), peripherals 0x%lx-0x%lx\n",
    __func__,
    SystemMemoryBase,
    SystemMemoryEnd - 1,
    FdBase,
    FdEnd - 1,
    (UINT64)X5_PERIPH_BASE,
    (UINT64)(X5_PERIPH_BASE + X5_PERIPH_SIZE - 1)
    ));

  *VirtualMemoryMap = VirtualMemoryTable;
}
