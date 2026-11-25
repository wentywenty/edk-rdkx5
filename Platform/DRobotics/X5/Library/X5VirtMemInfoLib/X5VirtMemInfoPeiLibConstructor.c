/** @file

  X5 ArmVirtMemInfoLib constructor.

  Publishes the non-secure DRAM size as a HOB so that the generic ArmVirt
  MemoryPeim can size the EFI system memory resource.  Unlike the QEMU variant
  there is no FDT to parse; the size comes straight from PcdSystemMemorySize.

  SPDX-License-Identifier: BSD-2-Clause-Patent

**/

#include <Uefi.h>
#include <Pi/PiMultiPhase.h>
#include <Library/BaseLib.h>
#include <Library/DebugLib.h>
#include <Library/HobLib.h>

RETURN_STATUS
EFIAPI
X5VirtMemInfoPeiLibConstructor (
  VOID
  )
{
  UINT64  Size;
  VOID    *Hob;

  Size = PcdGet64 (PcdSystemMemorySize);
  ASSERT (Size != 0);

  Hob = BuildGuidDataHob (&gArmVirtSystemMemorySizeGuid, &Size, sizeof Size);
  ASSERT (Hob != NULL);

  return RETURN_SUCCESS;
}
