#
#  X5 DXE firmware volume contents.
#
#  Derived from ArmVirtQemuFvMain.fdf.inc but with the QEMU/FDT/virtio/PCI
#  specific drivers removed: X5 is a real SoC with no virtio, no fw_cfg and no
#  PCIe root complex exposed to UEFI. Storage/media drivers for the X5
#  controllers (DesignWare MMC/SD, DWC SSI SPI-NAND) are added as they are
#  ported.
#
#  SPDX-License-Identifier: BSD-2-Clause-Patent
#

[FV.FvMain]
FvNameGuid         = 64074afe-340a-4be6-94ba-91b5b4d0f71e
BlockSize          = 0x40
NumBlocks          = 0         # This FV gets compressed so make it just big enough
FvAlignment        = 16        # FV alignment and FV attributes setting.
ERASE_POLARITY     = 1
MEMORY_MAPPED      = TRUE
STICKY_WRITE       = TRUE
LOCK_CAP           = TRUE
LOCK_STATUS        = TRUE
WRITE_DISABLED_CAP = TRUE
WRITE_ENABLED_CAP  = TRUE
WRITE_STATUS       = TRUE
WRITE_LOCK_CAP     = TRUE
WRITE_LOCK_STATUS  = TRUE
READ_DISABLED_CAP  = TRUE
READ_ENABLED_CAP   = TRUE
READ_STATUS        = TRUE
READ_LOCK_CAP      = TRUE
READ_LOCK_STATUS   = TRUE

  INF MdeModulePkg/Core/Dxe/DxeMain.inf
  INF MdeModulePkg/Universal/PCD/Dxe/Pcd.inf

  #
  # PI DXE Drivers producing Architectural Protocols (EFI Services)
  #
  INF ArmPkg/Drivers/CpuDxe/CpuDxe.inf
  INF MdeModulePkg/Core/RuntimeDxe/RuntimeDxe.inf
  INF MdeModulePkg/Universal/SecurityStubDxe/SecurityStubDxe.inf
  INF MdeModulePkg/Universal/CapsuleRuntimeDxe/CapsuleRuntimeDxe.inf
  INF MdeModulePkg/Universal/Variable/RuntimeDxe/VariableRuntimeDxe.inf
  INF MdeModulePkg/Universal/MonotonicCounterRuntimeDxe/MonotonicCounterRuntimeDxe.inf
  INF MdeModulePkg/Universal/ResetSystemRuntimeDxe/ResetSystemRuntimeDxe.inf
  INF EmbeddedPkg/RealTimeClockRuntimeDxe/RealTimeClockRuntimeDxe.inf
  INF EmbeddedPkg/MetronomeDxe/MetronomeDxe.inf
  INF MdeModulePkg/Universal/HiiDatabaseDxe/HiiDatabaseDxe.inf

  #
  # Multiple Console IO support
  #
  INF MdeModulePkg/Universal/Console/ConPlatformDxe/ConPlatformDxe.inf
  INF MdeModulePkg/Universal/Console/ConSplitterDxe/ConSplitterDxe.inf
  INF MdeModulePkg/Universal/Console/GraphicsConsoleDxe/GraphicsConsoleDxe.inf
  INF MdeModulePkg/Universal/Console/TerminalDxe/TerminalDxe.inf
  INF MdeModulePkg/Universal/SerialDxe/SerialDxe.inf

  #
  # SoC interrupt / timer / misc
  #
  INF ArmPkg/Drivers/ArmGicDxe/ArmGicDxe.inf
  INF ArmPkg/Drivers/TimerDxe/TimerDxe.inf
  INF MdeModulePkg/Universal/WatchdogTimerDxe/WatchdogTimer.inf
  INF SecurityPkg/RandomNumberGenerator/RngDxe/RngDxe.inf

  #
  # X5 SD card block I/O
  #
  INF Platform/DRobotics/X5/Drivers/X5SdhciDxe/X5SdhciDxe.inf

  #
  # FAT filesystem + GPT/MBR partitioning + UDF filesystem
  #
  INF MdeModulePkg/Universal/Disk/DiskIoDxe/DiskIoDxe.inf
  INF MdeModulePkg/Universal/Disk/PartitionDxe/PartitionDxe.inf
  INF FatPkg/EnhancedFatDxe/Fat.inf
  INF MdeModulePkg/Universal/Disk/UnicodeCollation/EnglishDxe/EnglishDxe.inf
  INF MdeModulePkg/Universal/Disk/UdfDxe/UdfDxe.inf

  #
  # Status Code Routing
  #
  INF MdeModulePkg/Universal/ReportStatusCodeRouter/RuntimeDxe/ReportStatusCodeRouterRuntimeDxe.inf

!include OvmfPkg/Include/Fdf/ShellDxe.fdf.inc

  #
  # Bds
  #
  INF MdeModulePkg/Universal/DevicePathDxe/DevicePathDxe.inf
  INF MdeModulePkg/Universal/DisplayEngineDxe/DisplayEngineDxe.inf
  INF MdeModulePkg/Universal/SetupBrowserDxe/SetupBrowserDxe.inf
  INF MdeModulePkg/Universal/DriverHealthManagerDxe/DriverHealthManagerDxe.inf
  INF MdeModulePkg/Universal/BdsDxe/BdsDxe.inf
  INF MdeModulePkg/Application/UiApp/UiApp.inf
  INF MdeModulePkg/Application/BootManagerMenuApp/BootManagerMenuApp.inf

  #
  # SMBIOS / ACPI table infrastructure (no tables produced yet)
  #
  INF MdeModulePkg/Universal/SmbiosDxe/SmbiosDxe.inf
  INF MdeModulePkg/Universal/Acpi/AcpiTableDxe/AcpiTableDxe.inf

  #
  # Hash2 Protocol producer
  #
  INF SecurityPkg/Hash2DxeCrypto/Hash2DxeCrypto.inf

  #
  # TianoCore logo (splash screen)
  #
  INF MdeModulePkg/Logo/LogoDxe.inf

  #
  # Ramdisk support
  #
  INF MdeModulePkg/Universal/Disk/RamDiskDxe/RamDiskDxe.inf
