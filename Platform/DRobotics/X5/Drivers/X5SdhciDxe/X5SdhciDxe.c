/** @file
  Minimal SDHCI (DesignWare MSHC) block driver for the RDK X5 SD card slot.

  Targets SD0 at 0x35020000, 3.3 V, default speed, 4-bit bus, PIO transfers.
  High speed / UHS / DDR / HS200 modes (and the X5 DLL/PHY tuning required for
  them) are intentionally not enabled yet: default speed is enough to bring up
  the FAT ESP and boot a payload.

  The card powers up in 3.3 V mode; the SLOT power switch is driven through
  DesignWare GPIO0 pin 26 and the HSIO clock gates / controller reset live in
  the X5 HSIO block (see x5_sdhci.c in the vendor U-Boot for the reference).

  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#include <Uefi.h>
#include <Library/BaseLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/DebugLib.h>
#include <Library/DevicePathLib.h>
#include <Library/IoLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiLib.h>
#include <Protocol/BlockIo.h>
#include <Protocol/DevicePath.h>

//
// SDHCI register offsets (SD Host Controller Simplified Specification).
//
#define SDHCI_BLOCK_SIZE        0x04
#define SDHCI_BLOCK_COUNT       0x06
#define SDHCI_ARGUMENT          0x08
#define SDHCI_TRANSFER_MODE     0x0C
#define SDHCI_COMMAND           0x0E
#define SDHCI_RESPONSE          0x10
#define SDHCI_BUFFER            0x20
#define SDHCI_PRESENT_STATE     0x24
#define SDHCI_HOST_CONTROL      0x28
#define SDHCI_POWER_CONTROL     0x29
#define SDHCI_CLOCK_CONTROL     0x2C
#define SDHCI_TIMEOUT_CONTROL   0x2E
#define SDHCI_SOFTWARE_RESET    0x2F
#define SDHCI_INT_STATUS        0x30
#define SDHCI_ERR_STATUS        0x32
#define SDHCI_INT_ENABLE        0x34
#define SDHCI_ERR_ENABLE        0x36
#define SDHCI_HOST_CONTROL2     0x3E
#define SDHCI_CAPABILITIES      0x40
#define SDHCI_HOST_VERSION      0xFE

// Present State bits
#define SDHCI_CMD_INHIBIT       BIT0
#define SDHCI_DATA_INHIBIT      BIT1
#define SDHCI_DOING_WRITE       BIT8
#define SDHCI_DOING_READ        BIT9
#define SDHCI_SPACE_AVAILABLE   BIT10
#define SDHCI_DATA_AVAILABLE    BIT11
#define SDHCI_CARD_PRESENT      BIT16

// Transfer Mode bits
#define SDHCI_TRNS_BLK_CNT_EN   BIT1
#define SDHCI_TRNS_READ         BIT4
#define SDHCI_TRNS_MULTI        BIT5

// Command flags (low byte of the command register)
#define SDHCI_CMD_RESP_NONE       0x00
#define SDHCI_CMD_RESP_LONG       0x01
#define SDHCI_CMD_RESP_SHORT      0x02
#define SDHCI_CMD_RESP_SHORT_BUSY 0x03
#define SDHCI_CMD_CRC             0x08
#define SDHCI_CMD_INDEX           0x10
#define SDHCI_CMD_DATA            0x20

// Clock Control bits
#define SDHCI_CLOCK_INT_EN      BIT0
#define SDHCI_CLOCK_INT_STABLE  BIT1
#define SDHCI_CLOCK_CARD_EN     BIT2

// Power
#define SDHCI_POWER_ON          0x01
#define SDHCI_POWER_330         0x0E

// Software reset
#define SDHCI_RESET_ALL         0x01
#define SDHCI_RESET_CMD         0x02
#define SDHCI_RESET_DATA        0x04

// Interrupt status bits
#define SDHCI_INT_RESPONSE      BIT0
#define SDHCI_INT_DATA_END      BIT1
#define SDHCI_INT_DATA_AVAIL    BIT5
#define SDHCI_INT_TIMEOUT       BIT16
#define SDHCI_INT_CRC           BIT17
#define SDHCI_INT_END_BIT       BIT18
#define SDHCI_INT_INDEX         BIT19
#define SDHCI_INT_DATA_TIMEOUT  BIT20
#define SDHCI_INT_DATA_CRC      BIT21
#define SDHCI_INT_DATA_END_BIT  BIT22
#define SDHCI_INT_CMD_ERROR     (SDHCI_INT_TIMEOUT | SDHCI_INT_CRC | SDHCI_INT_END_BIT | SDHCI_INT_INDEX)
#define SDHCI_INT_DATA_ERROR    (SDHCI_INT_DATA_TIMEOUT | SDHCI_INT_DATA_CRC | SDHCI_INT_DATA_END_BIT)

// SD cards
#define SD_CMD0                 0
#define SD_CMD2                 2
#define SD_CMD3                 3
#define SD_CMD6                 6
#define SD_CMD7                 7
#define SD_CMD8                 8
#define SD_CMD9                 9
#define SD_CMD12                12
#define SD_CMD16                16
#define SD_CMD17                17
#define SD_CMD18                18
#define SD_CMD24                24
#define SD_CMD25                25
#define SD_CMD55                55
#define SD_ACMD6                6
#define SD_ACMD41               41

#define SD_BLOCK_SIZE           512

//
// X5 HSIO platform registers (see x5_sdhci.c).
//
#define X5_SD0_SDHCI_BASE       0x35020000
#define X5_HSIO_CLK_EN          0x342100A0
#define X5_HSIO_SW_RST          0x342100A4
#define X5_NOC_IDLE_REQ         0x31032000
#define X5_NOC_IDLE_STATUS      0x31032008
#define X5_GPIO0_BASE           0x35060000
#define X5_SD_POWER_GPIO        26

#define X5_SD_CLK_GATE          (BIT13 | BIT14 | BIT15 | BIT17)
#define X5_SD_NOC_BIT           BIT14
#define X5_SD_RST_BIT           BIT3

#define X5_SDHCI_BASE_CLOCK     200000000U

#define SDHCI_MMIO_READ8(Base, Reg)   MmioRead8 ((UINTN)(Base) + (Reg))
#define SDHCI_MMIO_READ16(Base, Reg)  MmioRead16 ((UINTN)(Base) + (Reg))
#define SDHCI_MMIO_READ32(Base, Reg)  MmioRead32 ((UINTN)(Base) + (Reg))
#define SDHCI_MMIO_WRITE8(Base, Reg, Val)  MmioWrite8 ((UINTN)(Base) + (Reg), (Val))
#define SDHCI_MMIO_WRITE16(Base, Reg, Val) MmioWrite16 ((UINTN)(Base) + (Reg), (Val))
#define SDHCI_MMIO_WRITE32(Base, Reg, Val) MmioWrite32 ((UINTN)(Base) + (Reg), (Val))

typedef struct {
  UINTN                    Base;
  UINT32                   Rca;
  UINT64                   Capacity;   // bytes
  BOOLEAN                  Sdhc;       // block addressing (SDHC/SDXC)
  EFI_BLOCK_IO_MEDIA       Media;
  EFI_BLOCK_IO_PROTOCOL    BlockIo;
} X5_SD_CARD;

STATIC CONST EFI_GUID  mX5SdVenHwGuid = {
  0x6d2f1a84, 0x3b7c, 0x4e51, { 0x9a, 0x0d, 0x2c, 0x7e, 0x41, 0xb6, 0x53, 0x8f }
};

STATIC X5_SD_CARD  mX5SdCard;

STATIC
VOID
SdhciDumpState (
  IN X5_SD_CARD  *Card
  )
{
  DEBUG ((
    DEBUG_INFO,
    "X5SD: ver=0x%04x caps=0x%08x state=0x%08x int=0x%04x err=0x%04x\n",
    SDHCI_MMIO_READ16 (Card->Base, SDHCI_HOST_VERSION),
    SDHCI_MMIO_READ32 (Card->Base, SDHCI_CAPABILITIES),
    SDHCI_MMIO_READ32 (Card->Base, SDHCI_PRESENT_STATE),
    SDHCI_MMIO_READ16 (Card->Base, SDHCI_INT_STATUS),
    SDHCI_MMIO_READ16 (Card->Base, SDHCI_ERR_STATUS)
    ));
}

STATIC
VOID
SdhciClearStatus (
  IN X5_SD_CARD  *Card
  )
{
  SDHCI_MMIO_WRITE16 (Card->Base, SDHCI_INT_STATUS, 0xFFFF);
  SDHCI_MMIO_WRITE16 (Card->Base, SDHCI_ERR_STATUS, 0xFFFF);
}

STATIC
EFI_STATUS
SdhciWaitBits (
  IN X5_SD_CARD  *Card,
  IN UINTN       Reg,
  IN UINT32      Mask,
  IN BOOLEAN     Set,
  IN UINTN       TimeoutUs
  )
{
  UINTN  Elapsed;

  for (Elapsed = 0; Elapsed < TimeoutUs; Elapsed += 100) {
    if (((SDHCI_MMIO_READ32 (Card->Base, Reg) & Mask) != 0) == Set) {
      return EFI_SUCCESS;
    }

    gBS->Stall (100);
  }

  return EFI_TIMEOUT;
}

/**
  X5-specific platform bring-up: release the SD controller from reset, enable
  the HSIO clock gates and power the SD slot through GPIO0.26.
**/
STATIC
VOID
X5SdPlatformInit (
  VOID
  )
{
  UINT32  Value;
  UINT32  Timeout;

  // Hold the NOC idle request for the SD controller, then pulse the reset.
  Value = MmioRead32 (X5_NOC_IDLE_REQ);
  MmioWrite32 (X5_NOC_IDLE_REQ, Value | X5_SD_NOC_BIT);
  for (Timeout = 0; Timeout < 1000; Timeout++) {
    if ((MmioRead32 (X5_NOC_IDLE_STATUS) & X5_SD_NOC_BIT) != 0) {
      break;
    }
    gBS->Stall (100);
  }

  Value = MmioRead32 (X5_HSIO_SW_RST);
  MmioWrite32 (X5_HSIO_SW_RST, Value | X5_SD_RST_BIT);
  MmioWrite32 (X5_HSIO_SW_RST, Value & ~X5_SD_RST_BIT);

  Value = MmioRead32 (X5_NOC_IDLE_REQ);
  MmioWrite32 (X5_NOC_IDLE_REQ, Value & ~X5_SD_NOC_BIT);

  // Enable the SD0 aclk/hclk/bclk/tclk gates.
  Value = MmioRead32 (X5_HSIO_CLK_EN);
  MmioWrite32 (X5_HSIO_CLK_EN, Value | X5_SD_CLK_GATE);

  // Power the SD slot: GPIO0 pin 26 (active high), with a short power cycle.
  Value = MmioRead32 (X5_GPIO0_BASE + 0x04);          // SWPORT DDR
  MmioWrite32 (X5_GPIO0_BASE + 0x04, Value | (1U << X5_SD_POWER_GPIO));
  Value = MmioRead32 (X5_GPIO0_BASE + 0x00);          // SWPORT DR
  MmioWrite32 (X5_GPIO0_BASE + 0x00, Value & ~(1U << X5_SD_POWER_GPIO));
  gBS->Stall (20000);
  MmioWrite32 (X5_GPIO0_BASE + 0x00, Value | (1U << X5_SD_POWER_GPIO));
  gBS->Stall (100000);
}

STATIC
EFI_STATUS
SdhciReset (
  IN X5_SD_CARD  *Card
  )
{
  EFI_STATUS  Status;

  SDHCI_MMIO_WRITE8 (Card->Base, SDHCI_SOFTWARE_RESET, SDHCI_RESET_ALL);
  Status = SdhciWaitBits (Card, SDHCI_SOFTWARE_RESET, SDHCI_RESET_ALL, FALSE, 100000);
  if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_ERROR, "X5SD: controller reset timeout\n"));
    return Status;
  }

  // 3.3 V, SDCLK off, timeout counter max.
  SDHCI_MMIO_WRITE8 (Card->Base, SDHCI_POWER_CONTROL, 0);
  SDHCI_MMIO_WRITE8 (Card->Base, SDHCI_TIMEOUT_CONTROL, 0x0E);
  SDHCI_MMIO_WRITE8 (Card->Base, SDHCI_POWER_CONTROL, SDHCI_POWER_330 | SDHCI_POWER_ON);

  // 1-bit bus, SDMA (unused for PIO), no high speed.
  SDHCI_MMIO_WRITE8 (Card->Base, SDHCI_HOST_CONTROL, 0);
  SDHCI_MMIO_WRITE16 (Card->Base, SDHCI_HOST_CONTROL2, 0);
  SDHCI_MMIO_WRITE16 (Card->Base, SDHCI_ERR_ENABLE, 0);
  SDHCI_MMIO_WRITE16 (Card->Base, SDHCI_INT_ENABLE, 0);

  SdhciClearStatus (Card);
  return EFI_SUCCESS;
}

STATIC
EFI_STATUS
SdhciSetClock (
  IN X5_SD_CARD  *Card,
  IN UINT32      Clock
  )
{
  UINT32  Div;
  UINT16  Value;

  // Disable the card clock and wait for command/data to go idle.
  Value  = SDHCI_MMIO_READ16 (Card->Base, SDHCI_CLOCK_CONTROL);
  Value &= ~(SDHCI_CLOCK_CARD_EN | SDHCI_CLOCK_INT_EN);
  SDHCI_MMIO_WRITE16 (Card->Base, SDHCI_CLOCK_CONTROL, Value);
  SdhciWaitBits (Card, SDHCI_PRESENT_STATE, SDHCI_CMD_INHIBIT | SDHCI_DATA_INHIBIT, FALSE, 20000);

  if (Clock == 0) {
    return EFI_SUCCESS;
  }

  // SDHCI 3.0+: N = base / (2 * clock), smallest even divisor.
  Div = 2;
  while (Div < 0xFFE) {
    if ((X5_SDHCI_BASE_CLOCK / Div) <= Clock) {
      break;
    }
    Div += 2;
  }
  Div >>= 1;

  Value  = (UINT16)((Div & 0xFF) << 8);
  Value |= (UINT16)(((Div & 0x300) >> 8) << 6);
  SDHCI_MMIO_WRITE16 (Card->Base, SDHCI_CLOCK_CONTROL, Value);

  Value |= SDHCI_CLOCK_INT_EN;
  SDHCI_MMIO_WRITE16 (Card->Base, SDHCI_CLOCK_CONTROL, Value);
  if (EFI_ERROR (SdhciWaitBits (Card, SDHCI_CLOCK_CONTROL, SDHCI_CLOCK_INT_STABLE, TRUE, 20000))) {
    DEBUG ((DEBUG_ERROR, "X5SD: internal clock never stabilised\n"));
    return EFI_TIMEOUT;
  }

  Value |= SDHCI_CLOCK_CARD_EN;
  SDHCI_MMIO_WRITE16 (Card->Base, SDHCI_CLOCK_CONTROL, Value);
  return EFI_SUCCESS;
}

STATIC
EFI_STATUS
SdhciSendCommand (
  IN  X5_SD_CARD  *Card,
  IN  UINT8       Index,
  IN  UINT32      Argument,
  IN  UINT16      Flags,
  OUT UINT32      *Response   OPTIONAL
  )
{
  UINT16  IntStatus;
  UINT16  Cmd;

  if (EFI_ERROR (SdhciWaitBits (
                   Card,
                   SDHCI_PRESENT_STATE,
                   SDHCI_CMD_INHIBIT | SDHCI_DATA_INHIBIT,
                   FALSE,
                   100000
                   )))
  {
    DEBUG ((DEBUG_ERROR, "X5SD: cmd %u inhibit timeout\n", Index));
    return EFI_TIMEOUT;
  }

  SdhciClearStatus (Card);

  SDHCI_MMIO_WRITE32 (Card->Base, SDHCI_ARGUMENT, Argument);
  SDHCI_MMIO_WRITE16 (Card->Base, SDHCI_TRANSFER_MODE, 0);

  Cmd = (UINT16)(((UINT16)Index << 8) | (Flags & 0xFF));
  SDHCI_MMIO_WRITE16 (Card->Base, SDHCI_COMMAND, Cmd);

  // Wait for the response, or an error/timeout for commands with no response.
  for (UINTN Spin = 0; Spin < 10000; Spin++) {
    IntStatus = SDHCI_MMIO_READ16 (Card->Base, SDHCI_INT_STATUS);
    if ((IntStatus & SDHCI_INT_RESPONSE) != 0) {
      break;
    }

    if ((SDHCI_MMIO_READ16 (Card->Base, SDHCI_ERR_STATUS) & SDHCI_INT_CMD_ERROR) != 0) {
      break;
    }

    gBS->Stall (100);
  }

  IntStatus = SDHCI_MMIO_READ16 (Card->Base, SDHCI_INT_STATUS);
  if ((SDHCI_MMIO_READ16 (Card->Base, SDHCI_ERR_STATUS) & SDHCI_INT_CMD_ERROR) != 0) {
    SdhciClearStatus (Card);
    return EFI_DEVICE_ERROR;
  }

  if ((Flags & 0x03) != SDHCI_CMD_RESP_NONE) {
    if ((IntStatus & SDHCI_INT_RESPONSE) == 0) {
      SdhciClearStatus (Card);
      return EFI_TIMEOUT;
    }

    if (Response != NULL) {
      Response[0] = SDHCI_MMIO_READ32 (Card->Base, SDHCI_RESPONSE + 0x0);
      if ((Flags & 0x03) == SDHCI_CMD_RESP_LONG) {
        Response[1] = SDHCI_MMIO_READ32 (Card->Base, SDHCI_RESPONSE + 0x4);
        Response[2] = SDHCI_MMIO_READ32 (Card->Base, SDHCI_RESPONSE + 0x8);
        Response[3] = SDHCI_MMIO_READ32 (Card->Base, SDHCI_RESPONSE + 0xC);
      }
    }
  }

  SdhciClearStatus (Card);
  return EFI_SUCCESS;
}

/**
  Single block PIO transfer. Reads or writes one 512-byte block at Lba.
**/
STATIC
EFI_STATUS
SdhciTransferBlock (
  IN X5_SD_CARD  *Card,
  IN UINT8       CmdIndex,
  IN UINT32      Argument,
  IN BOOLEAN     IsRead,
  IN OUT VOID    *Buffer
  )
{
  EFI_STATUS  Status;
  UINTN       Offset;
  UINT16      TransferMode;
  UINT16      Command;
  UINT32      Word;

  if (EFI_ERROR (SdhciWaitBits (
                   Card,
                   SDHCI_PRESENT_STATE,
                   SDHCI_CMD_INHIBIT | SDHCI_DATA_INHIBIT,
                   FALSE,
                   100000
                   )))
  {
    return EFI_TIMEOUT;
  }

  SdhciClearStatus (Card);

  SDHCI_MMIO_WRITE16 (Card->Base, SDHCI_BLOCK_SIZE, SD_BLOCK_SIZE);
  SDHCI_MMIO_WRITE16 (Card->Base, SDHCI_BLOCK_COUNT, 1);
  SDHCI_MMIO_WRITE32 (Card->Base, SDHCI_ARGUMENT, Argument);

  TransferMode = SDHCI_TRNS_BLK_CNT_EN | (IsRead ? SDHCI_TRNS_READ : 0);
  SDHCI_MMIO_WRITE16 (Card->Base, SDHCI_TRANSFER_MODE, TransferMode);

  Command = (UINT16)(((UINT16)CmdIndex << 8) |
                     SDHCI_CMD_DATA | SDHCI_CMD_CRC | SDHCI_CMD_INDEX |
                     SDHCI_CMD_RESP_SHORT);
  SDHCI_MMIO_WRITE16 (Card->Base, SDHCI_COMMAND, Command);

  if (IsRead) {
    if (EFI_ERROR (SdhciWaitBits (Card, SDHCI_PRESENT_STATE, SDHCI_DATA_AVAILABLE, TRUE, 500000))) {
      DEBUG ((DEBUG_ERROR, "X5SD: read data not available\n"));
      SdhciClearStatus (Card);
      return EFI_TIMEOUT;
    }

    for (Offset = 0; Offset < SD_BLOCK_SIZE; Offset += sizeof (UINT32)) {
      Word = SDHCI_MMIO_READ32 (Card->Base, SDHCI_BUFFER);
      CopyMem ((UINT8 *)Buffer + Offset, &Word, sizeof (Word));
    }
  } else {
    for (Offset = 0; Offset < SD_BLOCK_SIZE; Offset += sizeof (UINT32)) {
      if (EFI_ERROR (SdhciWaitBits (Card, SDHCI_PRESENT_STATE, SDHCI_SPACE_AVAILABLE, TRUE, 500000))) {
        DEBUG ((DEBUG_ERROR, "X5SD: write buffer not ready\n"));
        SdhciClearStatus (Card);
        return EFI_TIMEOUT;
      }
      CopyMem (&Word, (UINT8 *)Buffer + Offset, sizeof (Word));
      SDHCI_MMIO_WRITE32 (Card->Base, SDHCI_BUFFER, Word);
    }
  }

  // Wait for the data transfer to complete.
  Status = SdhciWaitBits (Card, SDHCI_INT_STATUS, SDHCI_INT_DATA_END, TRUE, 500000);
  if (EFI_ERROR (Status) ||
      ((SDHCI_MMIO_READ16 (Card->Base, SDHCI_ERR_STATUS) & SDHCI_INT_DATA_ERROR) != 0))
  {
    DEBUG ((DEBUG_ERROR, "X5SD: block transfer failed (err=0x%04x)\n",
            SDHCI_MMIO_READ16 (Card->Base, SDHCI_ERR_STATUS)));
    Status = EFI_DEVICE_ERROR;
  }

  SdhciClearStatus (Card);
  return Status;
}

STATIC
EFI_STATUS
SdSendAppCmd (
  IN  X5_SD_CARD  *Card,
  IN  UINT32      Argument
  )
{
  return SdhciSendCommand (
           Card,
           SD_CMD55,
           (Card->Rca << 16),
           SDHCI_CMD_RESP_SHORT | SDHCI_CMD_CRC | SDHCI_CMD_INDEX,
           NULL
           );
}

/**
  Cortex-A55 SD card initialisation: CMD0/CMD8/ACMD41/CMD2/CMD3/CMD9/CMD7,
  4-bit bus and 25 MHz. Returns the card capacity in bytes.
**/
STATIC
EFI_STATUS
SdInitCard (
  IN X5_SD_CARD  *Card
  )
{
  EFI_STATUS  Status;
  UINT32      Response[4];
  UINT32      Ocr;
  UINT32      Csd[4];
  UINT32      CSize;
  UINT32      CSizeMult;
  UINT32      ReadBlLen;
  UINT32      Vers;
  UINT64      Capacity;
  BOOLEAN     Sdhc;

  Card->Rca      = 0;
  Card->Capacity = 0;
  Card->Sdhc     = FALSE;

  // CMD0: go idle.
  Status = SdhciSendCommand (Card, SD_CMD0, 0, SDHCI_CMD_RESP_NONE, NULL);
  if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_ERROR, "X5SD: CMD0 failed\n"));
    return Status;
  }

  // CMD8: voltage check (SD v2). Failure means an SD v1 card.
  Status = SdhciSendCommand (
             Card,
             SD_CMD8,
             0x000001AA,
             SDHCI_CMD_RESP_SHORT | SDHCI_CMD_CRC | SDHCI_CMD_INDEX,
             Response
             );
  Sdhc = !EFI_ERROR (Status);

  // ACMD41: wait for the card to leave idle.
  for (UINTN Spin = 0; Spin < 100; Spin++) {
    Status = SdhciSendCommand (
               Card,
               SD_CMD55,
               0,
               SDHCI_CMD_RESP_SHORT | SDHCI_CMD_CRC | SDHCI_CMD_INDEX,
               NULL
               );
    if (EFI_ERROR (Status)) {
      continue;
    }

    // HCS bit only for SD v2; keep 3.3 V operating range.
    Status = SdhciSendCommand (
               Card,
               SD_ACMD41,
               Sdhc ? 0x40FF8000 : 0x00FF8000,
               SDHCI_CMD_RESP_SHORT,
               Response
               );
    if (!EFI_ERROR (Status) && (Response[0] & BIT31) != 0) {
      break;
    }

    gBS->Stall (10000);
  }

  if (EFI_ERROR (Status) || (Response[0] & BIT31) == 0) {
    DEBUG ((DEBUG_ERROR, "X5SD: ACMD41 failed\n"));
    return EFI_DEVICE_ERROR;
  }

  Ocr  = Response[0];
  Sdhc = (Ocr & BIT30) != 0;

  // CMD2: get CID.
  Status = SdhciSendCommand (
             Card,
             SD_CMD2,
             0,
             SDHCI_CMD_RESP_LONG | SDHCI_CMD_CRC,
             Response
             );
  if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_ERROR, "X5SD: CMD2 failed\n"));
    return Status;
  }

  // CMD3: get the relative card address.
  Status = SdhciSendCommand (
             Card,
             SD_CMD3,
             0,
             SDHCI_CMD_RESP_SHORT | SDHCI_CMD_CRC | SDHCI_CMD_INDEX,
             Response
             );
  if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_ERROR, "X5SD: CMD3 failed\n"));
    return Status;
  }
  Card->Rca = Response[0] >> 16;

  // CMD9: get CSD and decode the capacity.
  Status = SdhciSendCommand (
             Card,
             SD_CMD9,
             Card->Rca << 16,
             SDHCI_CMD_RESP_LONG | SDHCI_CMD_CRC,
             Csd
             );
  if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_ERROR, "X5SD: CMD9 failed\n"));
    return Status;
  }

  // Csd[3] = bits[127:96], Csd[0] = bits[31:0].
  Vers = (Csd[3] >> 30) & 0x3;
  if (Vers == 1) {
    CSize = (((Csd[2] & 0x3FF) << 2) | ((Csd[1] >> 30) & 0x3));
    Capacity = ((UINT64)CSize + 1) * 512 * 1024;
  } else if (Vers == 0) {
    CSize       = (((Csd[2] & 0x3FF) << 2) | ((Csd[1] >> 30) & 0x3));
    CSizeMult   = (Csd[1] >> 15) & 0x7;
    ReadBlLen   = (Csd[2] >> 16) & 0xF;
    Capacity    = ((UINT64)CSize + 1) << (CSizeMult + 2);
    Capacity   *= (1ULL << ReadBlLen);
  } else {
    Capacity = 0;
  }

  if (Capacity == 0) {
    DEBUG ((DEBUG_ERROR, "X5SD: unsupported CSD structure %u\n", Vers));
    return EFI_DEVICE_ERROR;
  }

  // CMD7: select the card (R1b).
  Status = SdhciSendCommand (
             Card,
             SD_CMD7,
             Card->Rca << 16,
             SDHCI_CMD_RESP_SHORT_BUSY | SDHCI_CMD_CRC | SDHCI_CMD_INDEX,
             NULL
             );
  if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_ERROR, "X5SD: CMD7 failed\n"));
    return Status;
  }

  // ACMD6: 4-bit bus width.
  Status = SdSendAppCmd (Card, Card->Rca << 16);
  if (!EFI_ERROR (Status)) {
    Status = SdhciSendCommand (
               Card,
               SD_ACMD6,
               2,
               SDHCI_CMD_RESP_SHORT | SDHCI_CMD_CRC | SDHCI_CMD_INDEX,
               NULL
               );
    if (!EFI_ERROR (Status)) {
      SDHCI_MMIO_WRITE8 (
        Card->Base,
        SDHCI_HOST_CONTROL,
        SDHCI_MMIO_READ8 (Card->Base, SDHCI_HOST_CONTROL) | BIT1
        );
    }
  }

  // CMD16: block length 512.
  SdhciSendCommand (
    Card,
    SD_CMD16,
    SD_BLOCK_SIZE,
    SDHCI_CMD_RESP_SHORT | SDHCI_CMD_CRC | SDHCI_CMD_INDEX,
    NULL
    );

  // Move to default speed 25 MHz.
  SdhciSetClock (Card, 25000000);

  Card->Sdhc     = Sdhc;
  Card->Capacity = Capacity;
  DEBUG ((
    DEBUG_INFO,
    "X5SD: card ready (RCA=0x%04x, %s%d, %lu MB)\n",
    Card->Rca,
    Sdhc ? "SDHC/SDXC" : "SDSC",
    0,
    Capacity / (1024U * 1024U)
    ));
  return EFI_SUCCESS;
}

STATIC
EFI_STATUS
EFIAPI
X5SdReadBlocks (
  IN EFI_BLOCK_IO_PROTOCOL  *This,
  IN UINT32                 MediaId,
  IN EFI_LBA                Lba,
  IN UINTN                  BufferSize,
  OUT VOID                  *Buffer
  )
{
  X5_SD_CARD  *Card;
  UINTN       Blocks;
  UINTN       Index;
  EFI_STATUS  Status;

  Card = BASE_CR (This, X5_SD_CARD, BlockIo);

  if ((MediaId != Card->Media.MediaId) || !Card->Media.MediaPresent) {
    return EFI_MEDIA_CHANGED;
  }

  if (Buffer == NULL) {
    return EFI_INVALID_PARAMETER;
  }

  if ((BufferSize % SD_BLOCK_SIZE) != 0) {
    return EFI_BAD_BUFFER_SIZE;
  }

  if (Lba > Card->Media.LastBlock) {
    return EFI_INVALID_PARAMETER;
  }

  Blocks = BufferSize / SD_BLOCK_SIZE;
  if (Lba + Blocks - 1 > Card->Media.LastBlock) {
    return EFI_INVALID_PARAMETER;
  }

  for (Index = 0; Index < Blocks; Index++) {
    Status = SdhciTransferBlock (
               Card,
               SD_CMD17,
               (UINT32)(Lba + Index),
               TRUE,
               (UINT8 *)Buffer + Index * SD_BLOCK_SIZE
               );
    if (EFI_ERROR (Status)) {
      return Status;
    }
  }

  return EFI_SUCCESS;
}

STATIC
EFI_STATUS
EFIAPI
X5SdWriteBlocks (
  IN EFI_BLOCK_IO_PROTOCOL  *This,
  IN UINT32                 MediaId,
  IN EFI_LBA                Lba,
  IN UINTN                  BufferSize,
  IN VOID                   *Buffer
  )
{
  X5_SD_CARD  *Card;
  UINTN       Blocks;
  UINTN       Index;
  EFI_STATUS  Status;

  Card = BASE_CR (This, X5_SD_CARD, BlockIo);

  if ((MediaId != Card->Media.MediaId) || !Card->Media.MediaPresent) {
    return EFI_MEDIA_CHANGED;
  }

  if (Buffer == NULL) {
    return EFI_INVALID_PARAMETER;
  }

  if ((BufferSize % SD_BLOCK_SIZE) != 0) {
    return EFI_BAD_BUFFER_SIZE;
  }

  if (Lba > Card->Media.LastBlock) {
    return EFI_INVALID_PARAMETER;
  }

  Blocks = BufferSize / SD_BLOCK_SIZE;
  if (Lba + Blocks - 1 > Card->Media.LastBlock) {
    return EFI_INVALID_PARAMETER;
  }

  for (Index = 0; Index < Blocks; Index++) {
    Status = SdhciTransferBlock (
               Card,
               SD_CMD24,
               (UINT32)(Lba + Index),
               FALSE,
               (UINT8 *)Buffer + Index * SD_BLOCK_SIZE
               );
    if (EFI_ERROR (Status)) {
      return Status;
    }
  }

  return EFI_SUCCESS;
}

STATIC
EFI_STATUS
EFIAPI
X5SdFlushBlocks (
  IN EFI_BLOCK_IO_PROTOCOL  *This
  )
{
  return EFI_SUCCESS;
}

STATIC
EFI_STATUS
EFIAPI
X5SdReset (
  IN EFI_BLOCK_IO_PROTOCOL  *This,
  IN BOOLEAN                ExtendedVerification
  )
{
  return EFI_SUCCESS;
}

STATIC
EFI_DEVICE_PATH_PROTOCOL *
X5SdBuildDevicePath (
  IN UINTN  Base
  )
{
  VENDOR_DEVICE_PATH  *VenHw;
  EFI_DEVICE_PATH_PROTOCOL  *End;

  VenHw = AllocateZeroPool (sizeof (VENDOR_DEVICE_PATH) + sizeof (UINT64));
  if (VenHw == NULL) {
    return NULL;
  }

  VenHw->Header.Type    = HARDWARE_DEVICE_PATH;
  VenHw->Header.SubType = HW_VENDOR_DP;
  SetDevicePathNodeLength (&VenHw->Header, sizeof (VENDOR_DEVICE_PATH) + sizeof (UINT64));
  CopyGuid (&VenHw->Guid, &mX5SdVenHwGuid);
  CopyMem ((UINT8 *)VenHw + sizeof (VENDOR_DEVICE_PATH), &Base, sizeof (UINT64));

  End = AllocatePool (sizeof (EFI_DEVICE_PATH_PROTOCOL));
  if (End == NULL) {
    FreePool (VenHw);
    return NULL;
  }
  End->Type    = END_DEVICE_PATH_TYPE;
  End->SubType = END_ENTIRE_DEVICE_PATH_SUBTYPE;
  SetDevicePathNodeLength (End, sizeof (EFI_DEVICE_PATH_PROTOCOL));

  return AppendDevicePathNode ((EFI_DEVICE_PATH_PROTOCOL *)VenHw, End);
}

EFI_STATUS
EFIAPI
X5SdhciDxeEntry (
  IN EFI_HANDLE        ImageHandle,
  IN EFI_SYSTEM_TABLE  *SystemTable
  )
{
  EFI_STATUS                 Status;
  EFI_DEVICE_PATH_PROTOCOL   *DevicePath;
  EFI_HANDLE                 Handle;

  ZeroMem (&mX5SdCard, sizeof (mX5SdCard));
  mX5SdCard.Base        = X5_SD0_SDHCI_BASE;

  X5SdPlatformInit ();

  Status = SdhciReset (&mX5SdCard);
  if (EFI_ERROR (Status)) {
    return EFI_SUCCESS;
  }

  SdhciDumpState (&mX5SdCard);

  Status = SdhciSetClock (&mX5SdCard, 400000);
  if (EFI_ERROR (Status)) {
    return EFI_SUCCESS;
  }

  Status = SdInitCard (&mX5SdCard);
  if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_WARN, "X5SD: card init failed, no media exposed\n"));
    return EFI_SUCCESS;
  }

  //
  // Install the block I/O protocol.
  //
  mX5SdCard.Media.MediaId          = 1;
  mX5SdCard.Media.RemovableMedia   = TRUE;
  mX5SdCard.Media.MediaPresent     = TRUE;
  mX5SdCard.Media.LogicalPartition = FALSE;
  mX5SdCard.Media.ReadOnly         = FALSE;
  mX5SdCard.Media.WriteCaching     = FALSE;
  mX5SdCard.Media.BlockSize        = SD_BLOCK_SIZE;
  mX5SdCard.Media.LastBlock        = (mX5SdCard.Capacity / SD_BLOCK_SIZE) - 1;
  mX5SdCard.Media.IoAlign          = 0;

  mX5SdCard.BlockIo.Revision    = EFI_BLOCK_IO_PROTOCOL_REVISION;
  mX5SdCard.BlockIo.Media       = &mX5SdCard.Media;
  mX5SdCard.BlockIo.Reset       = X5SdReset;
  mX5SdCard.BlockIo.ReadBlocks  = X5SdReadBlocks;
  mX5SdCard.BlockIo.WriteBlocks = X5SdWriteBlocks;
  mX5SdCard.BlockIo.FlushBlocks = X5SdFlushBlocks;

  DevicePath = X5SdBuildDevicePath (X5_SD0_SDHCI_BASE);
  if (DevicePath == NULL) {
    return EFI_OUT_OF_RESOURCES;
  }

  Handle = NULL;
  Status = gBS->InstallMultipleProtocolInterfaces (
                  &Handle,
                  &gEfiDevicePathProtocolGuid,
                  DevicePath,
                  &gEfiBlockIoProtocolGuid,
                  &mX5SdCard.BlockIo,
                  NULL
                  );
  if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_ERROR, "X5SD: failed to install protocols: %r\n", Status));
    FreePool (DevicePath);
    return Status;
  }

  DEBUG ((DEBUG_INFO, "X5SD: block I/O installed (%lu blocks)\n", mX5SdCard.Media.LastBlock + 1));
  return EFI_SUCCESS;
}
