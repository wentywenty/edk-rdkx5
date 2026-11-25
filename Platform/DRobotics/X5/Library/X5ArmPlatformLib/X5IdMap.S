// SPDX-License-Identifier: BSD-2-Clause-Patent
//
// Early identity map used by ArmPlatformPkg/Sec via ArmPlatformPeiBootAction.
//
// X5 is entered as BL33 at 0x8800_0000 (inside DRAM), so unlike the QEMU
// variant the DRAM window must be executable. The map only has to keep SEC/PEI
// alive until MemoryInitPeim installs the full MMU map from
// ArmVirtMemInfoLib, so it is deliberately coarse (1 GB blocks):
//
//   0x0_0000_0000 - 0x3_FFFF_FFFF  peripherals (UART/GIC/storage), device, XN
//   0x8_0000_0000 - 0xF_FFFF_FFFF  DRAM (includes the FD at 0x8800_0000), RX
//

  .set      TT_TYPE_BLOCK, 0x1
  .set      TT_TYPE_TABLE, 0x3

  .set      TT_AF, 0x1 << 10
  .set      TT_NG, 0x1 << 11
  .set      TT_XN, 0x3 << 53

  .set      TT_MT_DEV, 0x0 << 2                 // MAIR #0
  .set      TT_MT_MEM, (0x3 << 2) | (0x3 << 8)  // MAIR #3

  .set      BLOCK_DEV, TT_TYPE_BLOCK | TT_MT_DEV | TT_AF | TT_XN | TT_NG
  .set      BLOCK_MEM, TT_TYPE_BLOCK | TT_MT_MEM | TT_AF | TT_NG

  .globl    idmap
  .section  ".rodata.idmap", "a"
  .align    12

idmap:      /* level 0 */
  .quad     1f + TT_TYPE_TABLE
  .fill     511, 8, 0x0

  .align    12
1:          /* level 1, 1 GB block descriptors */
  .quad     BLOCK_DEV | (0x00000000)   /* 0x0_0000_0000 - 0x3_FFFF_FFFF: peripherals */
  .quad     0x0                        /* 0x4_0000_0000 - 0x7_FFFF_FFFF: unused */
  .quad     BLOCK_MEM | (0x80000000)   /* 0x8_0000_0000 - 0xB_FFFF_FFFF: DRAM (RX) */
  .quad     BLOCK_MEM | (0xC0000000)   /* 0xC_0000_0000 - 0xF_FFFF_FFFF: DRAM (RX) */
  .fill     508, 8, 0x0                /* remaining 1 GB entries */

AARCH64_BTI_NOTE()
